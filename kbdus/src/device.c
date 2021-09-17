/* SPDX-License-Identifier: GPL-2.0-only */
/* -------------------------------------------------------------------------- */

#include <kbdus.h>
#include <kbdus/config.h>
#include <kbdus/device.h>
#include <kbdus/inverter.h>
#include <kbdus/utilities.h>

#include <linux/atomic.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kernfs.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/task.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/build_bug.h>
#else
#include <linux/bug.h>
#endif

/* -------------------------------------------------------------------------- */

struct kbdus_device
{
    struct kbdus_device_config config;

    atomic_t state;

    struct kbdus_inverter *inverter;

    struct blk_mq_tag_set tag_set;

    struct gendisk *disk;
    struct task_struct *add_disk_task;
    struct completion add_disk_task_started;
};

/* -------------------------------------------------------------------------- */

// The major number for BDUS block devices.
static int kbdus_device_major_;

/* -------------------------------------------------------------------------- */

static bool kbdus_device_is_read_only_(const struct kbdus_device_config *config)
{
    return !config->supports_write && !config->supports_write_same
        && !config->supports_write_zeros && !config->supports_fua_write
        && !config->supports_discard && !config->supports_secure_erase;
}

static bool kbdus_device_is_valid_ioctl_command_(unsigned int command)
{
    size_t size;

    size = (size_t)_IOC_SIZE(command);

    switch (_IOC_DIR(command))
    {
    case _IOC_NONE:
        return size == 0;

    case _IOC_READ:
    case _IOC_WRITE:
    case _IOC_READ | _IOC_WRITE:
        return size > 0 && size < (1ull << 14);

    default:
        return false;
    }
}

/* -------------------------------------------------------------------------- */

static bool
    kbdus_device_validate_config_(const struct kbdus_device_config *config)
{
    bool valid;

    // ensure that reserved space is zeroed out

    valid = kbdus_array_is_zero_filled(config->reserved_1_)
        && kbdus_array_is_zero_filled(config->reserved_2_);

    // operations -- supports_fua_write implies supports_flush

    valid = valid && (!config->supports_fua_write || config->supports_flush);

    // attributes -- logical_block_size

    valid = valid
        && (kbdus_is_power_of_two(config->logical_block_size)
            && config->logical_block_size >= 512u
            && config->logical_block_size <= (u32)PAGE_SIZE);

    // attributes -- physical_block_size

    valid = valid
        && (config->physical_block_size == 0
            || (kbdus_is_power_of_two(config->physical_block_size)
                && config->physical_block_size >= config->logical_block_size
                && config->physical_block_size <= (u32)PAGE_SIZE));

    // attributes -- size

    valid = valid
        && (kbdus_is_positive_multiple_of(
            config->size,
            max(config->physical_block_size, config->logical_block_size)));

    // attributes -- max_read_write_size

    valid = valid
        && (config->max_read_write_size == 0
            || config->max_read_write_size >= (u32)PAGE_SIZE);

    // attributes -- max_write_same_size

    valid = valid
        && (config->max_write_same_size == 0
            || config->max_write_same_size >= config->logical_block_size);

    // attributes -- max_write_zeros_size

    valid = valid
        && (config->max_write_zeros_size == 0
            || config->max_write_zeros_size >= config->logical_block_size);

    // attributes -- max_discard_erase_size

    valid = valid
        && (config->max_discard_erase_size == 0
            || config->max_discard_erase_size >= config->logical_block_size);

    // attributes -- max_outstanding_reqs

    valid = valid && (config->max_outstanding_reqs > 0);

    // return result

    return valid;
}

// Adjust a *previously validated* device configuration.
static void kbdus_device_adjust_config_(struct kbdus_device_config *config)
{
    // physical_block_size

    if (config->physical_block_size == 0)
        config->physical_block_size = config->logical_block_size;

    // max_read_write_size

    BUILD_BUG_ON(KBDUS_DEFAULT_MAX_READ_WRITE_SIZE < (u32)PAGE_SIZE);

    BUILD_BUG_ON(
        KBDUS_DEFAULT_MAX_READ_WRITE_SIZE > KBDUS_HARD_MAX_READ_WRITE_SIZE);

    if (!config->supports_read && !config->supports_write
        && !config->supports_fua_write)
    {
        config->max_read_write_size = 0;
    }
    else if (config->max_read_write_size == 0)
    {
        config->max_read_write_size = clamp(
            KBDUS_DEFAULT_MAX_READ_WRITE_SIZE, (u32)PAGE_SIZE,
            round_down(
                KBDUS_HARD_MAX_READ_WRITE_SIZE, config->logical_block_size));
    }
    else
    {
        config->max_read_write_size = round_down(
            min(config->max_read_write_size, KBDUS_HARD_MAX_READ_WRITE_SIZE),
            config->logical_block_size);
    }

    // max_write_same_size

    if (!config->supports_write_same)
    {
        config->max_write_same_size = 0;
    }
    else
    {
        config->max_write_same_size = round_down(
            min_not_zero(config->max_write_same_size, U32_MAX),
            config->logical_block_size);
    }

    // max_write_zeros_size

    if (!config->supports_write_zeros)
    {
        config->max_write_zeros_size = 0;
    }
    else
    {
        config->max_write_zeros_size = round_down(
            min_not_zero(config->max_write_zeros_size, U32_MAX),
            config->logical_block_size);
    }

    // max_discard_erase_size

    if (!config->supports_discard && !config->supports_secure_erase)
    {
        config->max_discard_erase_size = 0;
    }
    else
    {
        config->max_discard_erase_size = round_down(
            min_not_zero(config->max_discard_erase_size, U32_MAX),
            config->logical_block_size);
    }

    // max_outstanding_reqs

    if (!config->supports_read && !config->supports_write
        && !config->supports_write_same && !config->supports_write_zeros
        && !config->supports_fua_write && !config->supports_flush
        && !config->supports_discard && !config->supports_secure_erase
        && !config->supports_ioctl)
    {
        config->max_outstanding_reqs = 1;
    }
    else
    {
        config->max_outstanding_reqs =
            min(config->max_outstanding_reqs, KBDUS_HARD_MAX_OUTSTANDING_REQS);
    }
}

/* -------------------------------------------------------------------------- */

// Atomically set or clear a request queue flag.
static void kbdus_device_queue_flag_(
    struct request_queue *queue, unsigned int flag, bool value)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

    if (value)
        blk_queue_flag_set(flag, queue);
    else
        blk_queue_flag_clear(flag, queue);

#else

    spin_lock(queue->queue_lock);

    if (value)
        queue_flag_set(flag, queue);
    else
        queue_flag_clear(flag, queue);

    spin_unlock(queue->queue_lock);

#endif
}

static void kbdus_device_config_request_queue_flags_(
    struct request_queue *queue, const struct kbdus_device_config *config)
{
    kbdus_device_queue_flag_(queue, QUEUE_FLAG_NONROT, !config->rotational);
    kbdus_device_queue_flag_(queue, QUEUE_FLAG_ADD_RANDOM, false);

    // discard requests

    kbdus_device_queue_flag_(
        queue, QUEUE_FLAG_DISCARD, (bool)config->supports_discard);

    // secure erase requests

    kbdus_device_queue_flag_(
        queue,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        QUEUE_FLAG_SECERASE,
#else
        QUEUE_FLAG_SECDISCARD,
#endif
        (bool)config->supports_secure_erase);

    // flush / fua_write requests

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
    blk_queue_write_cache(
        queue, (bool)config->supports_flush, (bool)config->supports_fua_write);
#else
    blk_queue_flush(
        queue,
        (config->supports_flush ? REQ_FLUSH : 0)
            | (config->supports_fua_write ? REQ_FUA : 0));
#endif
}

static void kbdus_device_config_request_queue_limits_(
    struct request_queue *queue, const struct kbdus_device_config *config)
{
    // logical / physical block sizes

    blk_queue_logical_block_size(
        queue, (unsigned int)config->logical_block_size);

    blk_queue_physical_block_size(
        queue, (unsigned int)config->physical_block_size);

    // read / write / fua write requests (max_hw_sectors must be >= PAGE_SECTORS
    // even if none of these requests are supported)

    blk_queue_max_hw_sectors(
        queue,
        (unsigned int)(max(config->max_read_write_size, (u32)PAGE_SIZE) / 512u));

    // write same requests

    if (config->supports_write_same)
    {
        blk_queue_max_write_same_sectors(
            queue, (unsigned int)(config->max_write_same_size / 512u));
    }

    // write zeros requests

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
    if (config->supports_write_zeros)
    {
        blk_queue_max_write_zeroes_sectors(
            queue, (unsigned int)(config->max_write_zeros_size / 512u));
    }
#endif

    // discard / secure erase requests

    if (config->supports_discard || config->supports_secure_erase)
    {
        blk_queue_max_discard_sectors(
            queue, (unsigned int)(config->max_discard_erase_size / 512u));

        queue->limits.discard_granularity =
            (unsigned int)config->logical_block_size;
    }
}

static void kbdus_set_scheduler_none_(struct request_queue *q)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)

    // Scheduler is already "none" due to the BLK_MQ_F_NO_SCHED_BY_DEFAULT flag.

#else

    // This ends up invoking elv_iosched_store(), which is not exported for
    // modules to use. Ugly, but works.

    struct kernfs_node *kn_attr;
    int ret;

    kn_attr = kernfs_find_and_get(q->kobj.sd, "scheduler");
    WARN_ON(!kn_attr);

    ret = q->kobj.ktype->sysfs_ops->store(&q->kobj, kn_attr->priv, "none", 4);
    WARN_ON(ret != 4);

#endif
}

static int kbdus_device_add_disk_(void *argument)
{
    struct kbdus_device *device;

    device = argument;

    // inform waiters that task started

    complete(&device->add_disk_task_started);

    // add disk

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    if (add_disk(device->disk) != 0)
    {
        kbdus_device_terminate(device);
        return 0; // return value is ignored
    }
#else
    add_disk(device->disk);
#endif

    // switch scheduler to "none"

    kbdus_set_scheduler_none_(device->disk->queue);

    // submit "device available" notification

    kbdus_inverter_submit_device_available_notification(device->inverter);

    // update device state (unless already terminated)

    atomic_cmpxchg(
        &device->state, KBDUS_DEVICE_STATE_UNAVAILABLE,
        KBDUS_DEVICE_STATE_ACTIVE);

    return 0; // return value is ignored
}

/* -------------------------------------------------------------------------- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)

static blk_status_t kbdus_device_mq_ops_queue_rq_(
    struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
    struct kbdus_device *device;

    device = hctx->queue->queuedata;

    return errno_to_blk_status(
        kbdus_inverter_submit_request(device->inverter, bd->rq));
}

#else

static int kbdus_device_mq_ops_queue_rq_(
    struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
    struct kbdus_device *device;

    device = hctx->queue->queuedata;

    if (kbdus_inverter_submit_request(device->inverter, bd->rq) == 0)
        return BLK_MQ_RQ_QUEUE_OK;
    else
        return BLK_MQ_RQ_QUEUE_ERROR;
}

#endif

static enum blk_eh_timer_return
    kbdus_device_mq_ops_timeout_(struct request *req, bool reserved)
{
    struct kbdus_device *device;

    device = req->q->queuedata;

    return kbdus_inverter_timeout_request(device->inverter, req);
}

static void kbdus_device_mq_ops_complete_(struct request *req)
{
    struct kbdus_inverter_pdu *pdu;

    pdu = blk_mq_rq_to_pdu(req);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
    blk_mq_end_request(req, errno_to_blk_status(pdu->error));
#else
    blk_mq_end_request(req, pdu->error);
#endif
}

static const struct blk_mq_ops kbdus_device_queue_ops_ = {
    .queue_rq = kbdus_device_mq_ops_queue_rq_,
    .timeout  = kbdus_device_mq_ops_timeout_,
    .complete = kbdus_device_mq_ops_complete_,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
    .map_queues = NULL,
#else
    .map_queue = blk_mq_map_queue,
#endif
};

/* -------------------------------------------------------------------------- */

static void *kbdus_device_ioctl_create_buffer_(
    unsigned int command, void __user *argument_usrptr)
{
    size_t size;
    void *buffer;

    if (_IOC_DIR(command) == _IOC_NONE)
        return NULL;

    size = (size_t)_IOC_SIZE(command);

    if (!kbdus_access_ok(
            _IOC_DIR(command) & _IOC_WRITE ? KBDUS_VERIFY_WRITE
                                           : KBDUS_VERIFY_READ,
            argument_usrptr, size))
    {
        return ERR_PTR(-EFAULT);
    }

    buffer = kzalloc(size, GFP_KERNEL);

    if (!buffer)
        return ERR_PTR(-ENOMEM);

    if (_IOC_DIR(command) & _IOC_READ)
    {
        // read-only or read-write argument, copy data from caller

        if (__copy_from_user(buffer, argument_usrptr, size) != 0)
        {
            kfree(buffer);
            return ERR_PTR(-EFAULT);
        }
    }

    return buffer;
}

static int kbdus_device_ioctl_submit_and_await_(
    struct gendisk *disk, unsigned int command, void __user *argument_usrptr)
{
    struct request *req;
    void *arg_buffer;
    struct kbdus_inverter_pdu *pdu;
    int ret;

    // get request

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
    req = blk_get_request(disk->queue, REQ_OP_DRV_IN, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    req = blk_get_request(disk->queue, REQ_OP_DRV_IN, GFP_KERNEL);
#else
    req           = blk_get_request(disk->queue, READ, GFP_KERNEL);
#endif

    if (IS_ERR(req))
        return PTR_ERR(req);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
    req->cmd_type = REQ_TYPE_DRV_PRIV;
#else
    req->cmd_type = REQ_TYPE_SPECIAL;
#endif

    // allocate argument buffer and copy user space data to it (if applicable)

    arg_buffer = kbdus_device_ioctl_create_buffer_(command, argument_usrptr);

    if (IS_ERR(arg_buffer))
    {
        ret = PTR_ERR(arg_buffer);
        goto out_put_request;
    }

    // submit and await request

    pdu = blk_mq_rq_to_pdu(req);

    pdu->ioctl_command  = command;
    pdu->ioctl_argument = arg_buffer;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    blk_execute_rq(disk, req, 0);
#else
    blk_execute_rq(disk->queue, disk, req, 0);
#endif

    ret = pdu->error_ioctl;

    // put request

    blk_put_request(req);

    // copy argument buffer to user space and free it (if applicable)

    if (ret == 0 && (_IOC_DIR(command) & _IOC_WRITE))
    {
        // success and write-only or read-write argument, copy data to caller

        if (__copy_to_user(argument_usrptr, arg_buffer, _IOC_SIZE(command))
            != 0)
            ret = -EFAULT;
    }

    kfree(arg_buffer);

    goto out;

out_put_request:
    blk_put_request(req);
out:
    return ret;
}

static int kbdus_device_ioctl_(
    struct block_device *bdev, fmode_t mode, unsigned int command,
    unsigned long argument)
{
    // the kernel lets the driver handle BLKFLSBUF and BLKROSET if it can, but
    // we don't and just let the kernel do what is right

    if (command == BLKFLSBUF || command == BLKROSET)
        return -ENOTTY;

    // validate command

    if (!kbdus_device_is_valid_ioctl_command_(command))
        return -ENOTTY;

    // submit ioctl as request and await completion

    return kbdus_device_ioctl_submit_and_await_(
        bdev->bd_disk, command, (void __user *)argument);
}

// File operations for devices.
static const struct block_device_operations kbdus_device_ops_ = {
    .owner        = THIS_MODULE,
    .ioctl        = kbdus_device_ioctl_,
    .compat_ioctl = kbdus_device_ioctl_,
};

/* -------------------------------------------------------------------------- */

static struct gendisk *kbdus_device_create_disk_(
    struct blk_mq_tag_set *tag_set, const struct kbdus_device_config *config)
{
    unsigned int tag_set_flags;
    int ret;
    int minors;
    struct gendisk *disk;

    // initialize tag set

    tag_set_flags = 0;

    if (config->merge_requests)
        tag_set_flags |= BLK_MQ_F_SHOULD_MERGE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    tag_set_flags |= BLK_MQ_F_NO_SCHED_BY_DEFAULT;
#endif

    memset(tag_set, 0, sizeof(*tag_set));

    tag_set->nr_hw_queues = 1;
    tag_set->queue_depth  = (unsigned int)config->max_outstanding_reqs;
    tag_set->numa_node    = NUMA_NO_NODE;
    tag_set->cmd_size     = (unsigned int)sizeof(struct kbdus_inverter_pdu);
    tag_set->flags        = tag_set_flags;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    tag_set->ops = &kbdus_device_queue_ops_;
#else
    tag_set->ops = (struct blk_mq_ops *)&kbdus_device_queue_ops_;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    tag_set->nr_maps = 1;
#endif

    // allocate tag set

    ret = blk_mq_alloc_tag_set(tag_set);

    if (ret != 0)
        return ERR_PTR(ret);

    // create disk

    minors = config->enable_partition_scanning ? DISK_MAX_PARTS : 1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)

    disk = blk_mq_alloc_disk(tag_set, NULL);

    if (IS_ERR(disk))
    {
        blk_mq_free_tag_set(tag_set);
        return disk;
    }

    disk->minors = minors;

#else

    disk = alloc_disk(minors);

    if (!disk)
    {
        blk_mq_free_tag_set(tag_set);
        return ERR_PTR(-EIO);
    }

#endif

    if (!config->enable_partition_scanning)
        disk->flags |= GENHD_FL_NO_PART_SCAN;

    disk->major       = kbdus_device_major_;
    disk->first_minor = (int)config->minor;
    disk->fops        = &kbdus_device_ops_;

    WARN_ON(
        snprintf(disk->disk_name, DISK_NAME_LEN, "bdus-%llu", config->id)
        >= DISK_NAME_LEN);

    set_capacity(disk, (sector_t)(config->size / 512ull));
    set_disk_ro(disk, (int)kbdus_device_is_read_only_(config));

    // create queue

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)

    // Queue was already created by blk_mq_alloc_disk().

#else

    disk->queue = blk_mq_init_queue(tag_set);

    if (IS_ERR(disk->queue))
    {
        ret = PTR_ERR(disk->queue);
        put_disk(disk);
        blk_mq_free_tag_set(tag_set);
        return ERR_PTR(ret);
    }

#endif

    kbdus_device_config_request_queue_flags_(disk->queue, config);
    kbdus_device_config_request_queue_limits_(disk->queue, config);

    // return disk

    return disk;
}

/* -------------------------------------------------------------------------- */

int __init kbdus_device_init(void)
{
    // register block device

    kbdus_device_major_ = register_blkdev(0, "bdus");

    if (kbdus_device_major_ < 0)
        return kbdus_device_major_;

    // success

    return 0;
}

void kbdus_device_exit(void)
{
    unregister_blkdev(kbdus_device_major_, "bdus");
}

/* -------------------------------------------------------------------------- */

u32 kbdus_device_get_major(void)
{
    return (u32)kbdus_device_major_;
}

int kbdus_device_validate_and_adjust_config(struct kbdus_device_config *config)
{
    if (!kbdus_device_validate_config_(config))
        return -EINVAL;

    kbdus_device_adjust_config_(config);

    return 0;
}

struct kbdus_device *
    kbdus_device_create(const struct kbdus_device_config *config)
{
    struct kbdus_device *device;
    int ret_error;

    // allocate and initialize device structure a bit

    device = kzalloc(sizeof(*device), GFP_KERNEL);

    if (!device)
    {
        ret_error = -ENOMEM;
        goto error;
    }

    device->config = *config;

    atomic_set(&device->state, KBDUS_DEVICE_STATE_UNAVAILABLE);

    // create inverter

    device->inverter = kbdus_inverter_create(config);

    if (IS_ERR(device->inverter))
    {
        ret_error = PTR_ERR(device->inverter);
        goto error_free_device;
    }

    // create task for adding the disk

    device->add_disk_task = kthread_create(
        kbdus_device_add_disk_, device, "kbdus_device_add_disk_");

    if (IS_ERR(device->add_disk_task))
    {
        ret_error = PTR_ERR(device->add_disk_task);
        goto error_destroy_inverter;
    }

    init_completion(&device->add_disk_task_started);

    // create disk

    device->disk = kbdus_device_create_disk_(&device->tag_set, config);

    if (IS_ERR(device->disk))
    {
        ret_error = PTR_ERR(device->disk);
        goto error_stop_add_disk_task;
    }

    device->disk->queue->queuedata = device;

    // "get" `add_disk_task` so that it is still around when
    // `kbdus_device_destroy()` and thus `kthread_stop()` is called, even if the
    // task already ended

    get_task_struct(device->add_disk_task);

    // run add_disk_task

    wake_up_process(device->add_disk_task);

    // ensure that add_disk_task has started (if it hasn't already); (if
    // add_disk_task never ran, add_disk() would never be called and
    // del_gendisk() in device destruction would mess up, as kthread_stop() will
    // make the thread not run at all if it still hadn't started running)

    wait_for_completion(&device->add_disk_task_started);

    // success

    return device;

    // failure

error_stop_add_disk_task:
    kthread_stop(device->add_disk_task);
error_destroy_inverter:
    kbdus_inverter_terminate(device->inverter);
    kbdus_inverter_destroy(device->inverter);
error_free_device:
    kfree(device);
error:
    return ERR_PTR(ret_error);
}

void kbdus_device_destroy(struct kbdus_device *device)
{
    // terminate inverter (which fails all pending and future requests)

    kbdus_inverter_terminate(device->inverter);

    // wait for add_disk_task to end

    kthread_stop(device->add_disk_task);
    put_task_struct(device->add_disk_task);

    // clean up disk and queue

    del_gendisk(device->disk);

    blk_cleanup_queue(device->disk->queue);
    put_disk(device->disk);

    // destroy inverter

    kbdus_inverter_destroy(device->inverter);

    // free tag set

    blk_mq_free_tag_set(&device->tag_set);

    // free device

    kfree(device);
}

enum kbdus_device_state
    kbdus_device_get_state(const struct kbdus_device *device)
{
    return atomic_read(&device->state);
}

const struct kbdus_device_config *
    kbdus_device_get_config(const struct kbdus_device *device)
{
    return &device->config;
}

bool kbdus_device_is_read_only(const struct kbdus_device *device)
{
    return kbdus_device_is_read_only_(&device->config);
}

dev_t kbdus_device_get_dev(const struct kbdus_device *device)
{
    return disk_devt(device->disk);
}

struct kbdus_inverter *
    kbdus_device_get_inverter(const struct kbdus_device *device)
{
    return device->inverter;
}

void kbdus_device_terminate(struct kbdus_device *device)
{
    atomic_set(&device->state, KBDUS_DEVICE_STATE_TERMINATED);
    kbdus_inverter_terminate(device->inverter);
}

void kbdus_device_deactivate(struct kbdus_device *device, bool flush)
{
    enum kbdus_device_state old_state;

    old_state = atomic_xchg(&device->state, KBDUS_DEVICE_STATE_INACTIVE);

    WARN_ON(old_state != KBDUS_DEVICE_STATE_ACTIVE);

    kbdus_inverter_deactivate(device->inverter, flush);
}

void kbdus_device_activate(struct kbdus_device *device)
{
    enum kbdus_device_state old_state;

    old_state = atomic_xchg(&device->state, KBDUS_DEVICE_STATE_ACTIVE);

    WARN_ON(old_state != KBDUS_DEVICE_STATE_INACTIVE);

    kbdus_inverter_activate(device->inverter);
    kbdus_inverter_submit_device_available_notification(device->inverter);
}

/* -------------------------------------------------------------------------- */
