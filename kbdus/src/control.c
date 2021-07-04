/* SPDX-License-Identifier: GPL-2.0-only */
/* -------------------------------------------------------------------------- */

#include <kbdus.h>
#include <kbdus/config.h>
#include <kbdus/control.h>
#include <kbdus/device.h>
#include <kbdus/transceiver.h>
#include <kbdus/utilities.h>

#include <linux/aio.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/stddef.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/wait.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/build_bug.h>
#else
#include <linux/bug.h>
#endif

/* -------------------------------------------------------------------------- */

static unsigned int kbdus_control_max_devices_ =
    (unsigned int)KBDUS_HARD_MAX_DEVICES;

module_param_named(max_devices, kbdus_control_max_devices_, uint, 0444);

MODULE_PARM_DESC(
    max_devices,
    "The maximum number of BDUS block devices that may exist simultaneously."
    " Must be positive and less than or equal to "
    __stringify(KBDUS_HARD_MAX_DEVICES) ". Defaults to "
    __stringify(KBDUS_HARD_MAX_DEVICES) "."
    );

static int validate_module_parameters_(void)
{
    // validate module parameter 'max_devices'

    if (kbdus_control_max_devices_ < 1u
        || kbdus_control_max_devices_ > (unsigned int)KBDUS_HARD_MAX_DEVICES)
    {
        printk(
            KERN_ERR
            "kbdus: Invalid value %u for module parameter 'max_devices', must "
            "be positive and less than or equal to " __stringify(
                KBDUS_HARD_MAX_DEVICES) ".\n",
            kbdus_control_max_devices_);

        return -EINVAL;
    }

    // parameters are valid

    return 0;
}

/* -------------------------------------------------------------------------- */

static const struct kbdus_version kbdus_control_version_ = {
    .major = (u32)KBDUS_HEADER_VERSION_MAJOR,
    .minor = (u32)KBDUS_HEADER_VERSION_MINOR,
    .patch = (u32)KBDUS_HEADER_VERSION_PATCH,
};

enum
{
    // Whether the fd is attached to a device.
    KBDUS_CONTROL_FD_FLAG_ATTACHED_,

    // Whether the fd was "marked as successful" using
    // KBDUS_IOCTL_MARK_AS_SUCCESSFUL ioctl.
    KBDUS_CONTROL_FD_FLAG_SUCCESSFUL_,
};

/* -------------------------------------------------------------------------- */

struct kbdus_control_device_wrapper_
{
    int index; // determines the device's minor numbers
    struct kbdus_device *device;

    struct kbdus_control_fd_ *fd; // NULL if no fd attached to device
    struct completion *on_detach;
};

// State of a file description (fd) for the control device.
struct kbdus_control_fd_
{
    unsigned long flags;

    // the fields bellow are only valid if flag KBDUS_CONTROL_FD_FLAG_ATTACHED_
    // is set

    struct kbdus_control_device_wrapper_ *device_wrapper;
    struct kbdus_transceiver *transceiver;
};

/* -------------------------------------------------------------------------- */

// Some necessary control device state.
static dev_t kbdus_control_devt_;
static struct cdev kbdus_control_cdev_;
static struct class *kbdus_control_class_;
static struct device *kbdus_control_device_;

static DEFINE_MUTEX(kbdus_control_mutex_);

/*
 * The id for the next device being created.
 *
 * Always modified with both `kbdus_control_mutex_` and
 * `kbdus_control_device_destroyed_.lock` held.
 */
static u64 kbdus_control_next_id_ = 0;

/*
 * The number of existing devices.
 *
 * Always modified with both `kbdus_control_mutex_` and
 * `kbdus_control_device_destroyed_.lock` held.
 */
static u32 kbdus_control_num_devices_ = 0;

/*
 * Maps device indices to device wrappers of existing devices.
 *
 * Always modified with both `kbdus_control_mutex_` and
 * `kbdus_control_device_destroyed_.lock` held.
 */
static DEFINE_IDR(kbdus_control_devices_);

// Triggered whenever a device is destroyed, after its wrapper is removed from
// `kbdus_control_devices_`.
static DECLARE_WAIT_QUEUE_HEAD(kbdus_control_device_destroyed_);

/* -------------------------------------------------------------------------- */

/*
 * Get the device wrapper for the device with the given id.
 *
 * Must be called with `kbdus_control_mutex_` or
 * `kbdus_control_device_destroyed_.lock` (or both) held.
 *
 * Never sleeps.
 *
 * - Returns ERR_PTR(-EINVAL) if no such device ever existed.
 * - Returns ERR_PTR(-ENODEV) if the device no longer exists.
 */
static struct kbdus_control_device_wrapper_ *
    kbdus_control_get_device_wrapper_by_id_(u64 id)
{
    struct kbdus_control_device_wrapper_ *device_wrapper;
    int index;

    if (id >= kbdus_control_next_id_)
        return ERR_PTR(-EINVAL); // device never existed

    idr_for_each_entry(&kbdus_control_devices_, device_wrapper, index)
    {
        if (kbdus_device_get_config(device_wrapper->device)->id == id)
            return device_wrapper; // found device
    }

    return ERR_PTR(-ENODEV); // device no longer exists
}

/*
 * Destroy a device.
 *
 * The device wrapper is first removed from `kbdus_control_devices_` and then
 * the device is destroyed and the device wrapper is freed.
 *
 * Must be called with `kbdus_control_mutex_` held. Acquires and releases
 * `kbdus_control_device_destroyed_.lock`.
 *
 * Might sleep.
 */
static void kbdus_control_destroy_device_(
    struct kbdus_control_device_wrapper_ *device_wrapper)
{
    // remove device wrapper from IDR and notify waiters

    spin_lock(&kbdus_control_device_destroyed_.lock);

    --kbdus_control_num_devices_;

    idr_remove(&kbdus_control_devices_, device_wrapper->index);
    wake_up_all_locked(&kbdus_control_device_destroyed_);

    spin_unlock(&kbdus_control_device_destroyed_.lock);

    // destroy device

    kbdus_device_destroy(device_wrapper->device);

    // free device wrapper

    kfree(device_wrapper);
}

/*
 * Flush the given `block_device`.
 *
 * This function is similar in function to `blkdev_fsync()`.
 *
 * Flushes the page cache of the given `block_device` (converting dirty pages
 * into "write" requests) and then sends it a "flush" request.
 */
static int kbdus_control_flush_bdev_(struct block_device *bdev)
{
    int ret;

    // flush block_device (similar to what is done in blkdev_fsync())

    kbdus_assert(bdev->bd_inode);
    kbdus_assert(bdev->bd_inode->i_mapping);

    ret = filemap_write_and_wait(bdev->bd_inode->i_mapping);

    if (ret == 0)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        ret = blkdev_issue_flush(bdev);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
        ret = blkdev_issue_flush(bdev, GFP_KERNEL);
#else
        ret = blkdev_issue_flush(bdev, GFP_KERNEL, NULL);
#endif

        if (ret == -EOPNOTSUPP)
            ret = 0;
    }

    // return result

    return ret;
}

/* -------------------------------------------------------------------------- */

static int kbdus_control_open_(struct inode *inode, struct file *filp)
{
    struct kbdus_control_fd_ *fd;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // allocate fd

    fd = kzalloc(sizeof(*fd), GFP_KERNEL);

    if (!fd)
        return -ENOMEM;

    // initialize fd

    clear_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags);
    clear_bit(KBDUS_CONTROL_FD_FLAG_SUCCESSFUL_, &fd->flags);

    // store fd in filp's private data

    filp->private_data = fd;

    // success

    return 0;
}

static int kbdus_control_release_(struct inode *inode, struct file *filp)
{
    struct kbdus_control_fd_ *fd;
    const struct kbdus_device_config *device_config;

    fd = filp->private_data;

    if (test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
    {
        mutex_lock(&kbdus_control_mutex_);

        device_config = kbdus_device_get_config(fd->device_wrapper->device);

        fd->device_wrapper->fd = NULL;

        kbdus_transceiver_destroy(fd->transceiver);

        switch (kbdus_device_get_state(fd->device_wrapper->device))
        {
        case KBDUS_DEVICE_STATE_UNAVAILABLE:

            kbdus_assert(!fd->device_wrapper->on_detach);

            kbdus_control_destroy_device_(fd->device_wrapper);

            break;

        case KBDUS_DEVICE_STATE_ACTIVE:

            if (!device_config->recoverable
                && !test_bit(KBDUS_CONTROL_FD_FLAG_SUCCESSFUL_, &fd->flags))
            {
                kbdus_device_terminate(fd->device_wrapper->device);
            }
            else
            {
                kbdus_device_deactivate(fd->device_wrapper->device, false);
            }

            if (fd->device_wrapper->on_detach)
                complete(fd->device_wrapper->on_detach);
            else if (!device_config->recoverable)
                kbdus_control_destroy_device_(fd->device_wrapper);

            break;

        case KBDUS_DEVICE_STATE_INACTIVE:

            if (!device_config->recoverable
                && !test_bit(KBDUS_CONTROL_FD_FLAG_SUCCESSFUL_, &fd->flags))
            {
                kbdus_device_terminate(fd->device_wrapper->device);
            }

            if (fd->device_wrapper->on_detach)
                complete(fd->device_wrapper->on_detach);
            else if (!device_config->recoverable)
                kbdus_control_destroy_device_(fd->device_wrapper);

            break;

        case KBDUS_DEVICE_STATE_TERMINATED:

            if (fd->device_wrapper->on_detach)
                complete(fd->device_wrapper->on_detach);
            else
                kbdus_control_destroy_device_(fd->device_wrapper);

            break;

        default:
            kbdus_assert(false);
            break;
        }

        mutex_unlock(&kbdus_control_mutex_);
    }

    kfree(fd);

    return 0;
}

static int kbdus_control_ioctl_get_version_(
    struct kbdus_version __user *version_usrptr)
{
    // copy version to user space

    if (copy_to_user(
            version_usrptr, &kbdus_control_version_,
            sizeof(kbdus_control_version_))
        != 0)
    {
        return -EFAULT;
    }

    // success

    return 0;
}

// Must be called with `kbdus_control_mutex_` held.
static int __kbdus_control_ioctl_create_device_(
    struct kbdus_control_fd_ *fd, struct kbdus_device_and_fd_config *config,
    struct kbdus_device_and_fd_config __user *config_usrptr)
{
    int index;
    int ret;
    struct kbdus_control_device_wrapper_ *device_wrapper;
    struct kbdus_transceiver *transceiver;
    void *prev_ptr;

    // ensure that fd is attached to a device

    if (test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
        return -EINVAL;

    // check if limit of simultaneously existing device has not been reached

    if (kbdus_control_num_devices_ == (u32)kbdus_control_max_devices_)
        return -ENOSPC;

    // find index for device

    index = idr_alloc_cyclic(
        &kbdus_control_devices_, NULL, 0, (1 << MINORBITS) / DISK_MAX_PARTS,
        GFP_KERNEL);

    kbdus_assert(index != -ENOSPC);

    if (index < 0)
        return index;

    // assign id to device

    config->device.id = kbdus_control_next_id_;

    // copy modified configuration back to user space

    if (copy_to_user(config_usrptr, config, sizeof(*config)) != 0)
    {
        ret = -EFAULT;
        goto out_remove_idr;
    }

    // create device wrapper

    device_wrapper = kzalloc(sizeof(*device_wrapper), GFP_KERNEL);

    if (!device_wrapper)
    {
        ret = -ENOMEM;
        goto out_remove_idr;
    }

    device_wrapper->index     = index;
    device_wrapper->fd        = fd;
    device_wrapper->on_detach = NULL;

    // create device

    device_wrapper->device =
        kbdus_device_create(&config->device, index * DISK_MAX_PARTS);

    if (IS_ERR(device_wrapper->device))
    {
        ret = PTR_ERR(device_wrapper->device);
        goto out_free_device_wrapper;
    }

    // create transceiver

    transceiver = kbdus_transceiver_create(
        config, kbdus_device_get_inverter(device_wrapper->device));

    if (IS_ERR(transceiver))
    {
        ret = PTR_ERR(transceiver);
        goto out_destroy_device;
    }

    // add device wrapper to IDR and attach fd to device

    spin_lock(&kbdus_control_device_destroyed_.lock);

    ++kbdus_control_next_id_;
    ++kbdus_control_num_devices_;

    prev_ptr = idr_replace(&kbdus_control_devices_, device_wrapper, index);

    spin_unlock(&kbdus_control_device_destroyed_.lock);

    kbdus_assert(!prev_ptr);

    fd->device_wrapper = device_wrapper;
    fd->transceiver    = transceiver;

    set_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags);

    return 0;

out_destroy_device:
    kbdus_device_destroy(device_wrapper->device);
out_free_device_wrapper:
    kfree(device_wrapper);
out_remove_idr:
    idr_remove(&kbdus_control_devices_, index);
    return ret;
}

static int kbdus_control_ioctl_create_device_(
    struct kbdus_control_fd_ *fd,
    struct kbdus_device_and_fd_config __user *config_usrptr)
{
    struct kbdus_device_and_fd_config config;
    int ret;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device configuration from user space

    if (copy_from_user(&config, config_usrptr, sizeof(config)) != 0)
        return -EFAULT;

    // validate and adjust device configuration

    ret = kbdus_device_validate_and_adjust_config(&config.device);

    if (ret != 0)
        return ret;

    // validate and adjust transceiver configuration

    ret = kbdus_transceiver_validate_and_adjust_config(&config);

    if (ret != 0)
        return ret;

    // lock global mutex

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // delegate remaining work

    ret = __kbdus_control_ioctl_create_device_(fd, &config, config_usrptr);

    // unlock global mutex

    mutex_unlock(&kbdus_control_mutex_);

    // return result

    return ret;
}

// Must be called with `kbdus_control_mutex_` held.
static int __kbdus_control_ioctl_attach_to_device_(
    struct kbdus_control_fd_ *fd,
    struct kbdus_control_device_wrapper_ *device_wrapper,
    const struct kbdus_device_and_fd_config *config)
{
    struct kbdus_transceiver *transceiver;
    DECLARE_COMPLETION_ONSTACK(on_detach);

    // ensure that device is available

    if (kbdus_device_get_state(device_wrapper->device)
        == KBDUS_DEVICE_STATE_UNAVAILABLE)
    {
        return -EBUSY;
    }

    // ensure that no one else is trying to attach to the device

    if (device_wrapper->on_detach)
        return -EINPROGRESS;

    // check if device has fd attached to it

    if (device_wrapper->fd)
    {
        // deactivate device (unless already terminated, in which case we use
        // the completion simply to wait until the device is destroyed)

        if (kbdus_device_get_state(device_wrapper->device)
            != KBDUS_DEVICE_STATE_TERMINATED)
        {
            kbdus_device_deactivate(device_wrapper->device, true);
        }

        // wait until attached fd detaches from device

        device_wrapper->on_detach = &on_detach;

        mutex_unlock(&kbdus_control_mutex_);

        if (wait_for_completion_interruptible(&on_detach) == 0)
        {
            // waited until fd detached

            mutex_lock(&kbdus_control_mutex_);

            device_wrapper->on_detach = NULL;

            kbdus_assert(!device_wrapper->fd);
        }
        else
        {
            // wait interrupted, fd may or may not have detached from
            // device

            mutex_lock(&kbdus_control_mutex_);

            device_wrapper->on_detach = NULL;

            if (device_wrapper->fd)
                return -ERESTARTSYS;
        }

        // destroy device if it was already terminated

        if (kbdus_device_get_state(device_wrapper->device)
            == KBDUS_DEVICE_STATE_TERMINATED)
        {
            kbdus_control_destroy_device_(device_wrapper);
            return -ENODEV;
        }
    }

    kbdus_assert(
        kbdus_device_get_state(device_wrapper->device)
        == KBDUS_DEVICE_STATE_INACTIVE);

    // create transceiver

    transceiver = kbdus_transceiver_create(
        config, kbdus_device_get_inverter(device_wrapper->device));

    if (IS_ERR(transceiver))
    {
        if (!config->device.recoverable)
            kbdus_control_destroy_device_(device_wrapper);

        return PTR_ERR(transceiver);
    }

    // activate device

    kbdus_device_activate(device_wrapper->device);

    // attach fd to device

    device_wrapper->fd = fd;

    fd->device_wrapper = device_wrapper;
    fd->transceiver    = transceiver;

    set_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags);

    // success

    return 0;
}

static int kbdus_control_ioctl_attach_to_device_(
    struct kbdus_control_fd_ *fd,
    struct kbdus_device_and_fd_config __user *config_usrptr)
{
    struct kbdus_device_and_fd_config config;
    int ret;
    struct kbdus_control_device_wrapper_ *device_wrapper;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device id and fd configuration from user space

    if (get_user(config.device.id, &config_usrptr->device.id) != 0)
        return -EFAULT;

    if (copy_from_user(&config.fd, &config_usrptr->fd, sizeof(config.fd)) != 0)
        return -EFAULT;

    // lock global mutex

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // ensure that fd is not attached to a device

    if (test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
    {
        ret = -EINVAL;
        goto out_unlock;
    }

    // get device wrapper

    device_wrapper = kbdus_control_get_device_wrapper_by_id_(config.device.id);

    if (IS_ERR(device_wrapper))
    {
        ret = PTR_ERR(device_wrapper);
        goto out_unlock;
    }

    // get device configuration

    config.device = *kbdus_device_get_config(device_wrapper->device);

    // validate and adjust fd configuration

    ret = kbdus_transceiver_validate_and_adjust_config(&config);

    if (ret != 0)
        goto out_unlock;

    // copy configuration to user space

    if (copy_to_user(config_usrptr, &config, sizeof(config)) != 0)
    {
        ret = -EFAULT;
        goto out_unlock;
    }

    // delegate remaining work

    ret = __kbdus_control_ioctl_attach_to_device_(fd, device_wrapper, &config);

out_unlock:
    mutex_unlock(&kbdus_control_mutex_);
    return ret;
}

static int kbdus_control_ioctl_terminate_(struct kbdus_control_fd_ *fd)
{
    struct kbdus_device *device;

    if (!test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
        return -EINVAL;

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    device = fd->device_wrapper->device;

    switch (kbdus_device_get_state(device))
    {
    case KBDUS_DEVICE_STATE_UNAVAILABLE:
        kbdus_device_terminate(device);
        break;

    case KBDUS_DEVICE_STATE_ACTIVE:
        if (!kbdus_device_get_config(device)->recoverable)
            kbdus_device_terminate(device);
        else
            kbdus_device_deactivate(device, false);
        break;

    case KBDUS_DEVICE_STATE_INACTIVE:
        if (!kbdus_device_get_config(device)->recoverable)
            kbdus_device_terminate(device);
        break;

    case KBDUS_DEVICE_STATE_TERMINATED:
        break;

    default:
        kbdus_assert(false);
        break;
    }

    mutex_unlock(&kbdus_control_mutex_);

    return 0;
}

static int kbdus_control_ioctl_mark_as_successful_(struct kbdus_control_fd_ *fd)
{
    set_bit(KBDUS_CONTROL_FD_FLAG_SUCCESSFUL_, &fd->flags);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)

static int kbdus_control_stat_impl_(
    const char __user *filename, struct kstat *stat, bool reval)
{
    unsigned int lookup_flags;
    int error;
    struct path path;

    lookup_flags = LOOKUP_FOLLOW | (reval ? LOOKUP_REVAL : 0);

    error = user_path_at_empty(AT_FDCWD, filename, lookup_flags, &path, NULL);

    if (error != 0)
        return error;

    error = vfs_getattr(&path, stat, STATX_BASIC_STATS, AT_NO_AUTOMOUNT);

    path_put(&path);

    return error;
}

#endif

static int kbdus_control_stat_(const char __user *filename, struct kstat *stat)
{
    int error;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)

    error = kbdus_control_stat_impl_(filename, stat, false);

    if (error == -ESTALE)
        error = kbdus_control_stat_impl_(filename, stat, true);

#else

    error = vfs_stat(filename, stat);

#endif

    return error;
}

static int kbdus_control_ioctl_device_path_to_id_(
    struct kbdus_control_fd_ *fd, u64 __user *path_or_id_usrptr)
{
    u64 path_usrptr;
    struct kstat stat;
    int ret;
    struct kbdus_control_device_wrapper_ *device_wrapper;
    u64 id;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy pointer to path from user space

    if (get_user(path_usrptr, path_or_id_usrptr) != 0)
        return -EFAULT;

    // lock global mutex

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // stat file

    ret = kbdus_control_stat_((const char __user *)path_usrptr, &stat);

    if (ret != 0)
    {
        mutex_unlock(&kbdus_control_mutex_);
        return ret;
    }

    // ensure that file is block device

    if (!S_ISBLK(stat.mode))
    {
        mutex_unlock(&kbdus_control_mutex_);
        return -ENOTBLK;
    }

    // ensure that dev_t is in the range for BDUS devices

    if (MAJOR(stat.rdev) != kbdus_device_get_major())
    {
        mutex_unlock(&kbdus_control_mutex_);
        return -EINVAL;
    }

    // get device wrapper

    device_wrapper =
        idr_find(&kbdus_control_devices_, MINOR(stat.rdev) / DISK_MAX_PARTS);

    if (!device_wrapper)
    {
        // dev_t does not correspond to an existing device or partition thereof
        mutex_unlock(&kbdus_control_mutex_);
        return -ENODEV;
    }

    // store id (as device may be destroyed after unlocking the global mutex)

    id = kbdus_device_get_config(device_wrapper->device)->id;

    // unlock global mutex

    mutex_unlock(&kbdus_control_mutex_);

    // ensure that dev_t corresponds to whole device

    if (MINOR(stat.rdev) % (u32)DISK_MAX_PARTS != 0)
        return -ECHILD;

    // copy device id to user space

    if (put_user(id, path_or_id_usrptr) != 0)
        return -EFAULT;

    // success

    return 0;
}

static int kbdus_control_ioctl_get_device_config_(
    struct kbdus_device_config __user *device_config_usrptr)
{
    u64 id;
    int ret;
    struct kbdus_control_device_wrapper_ *device_wrapper;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device id from user space

    if (get_user(id, &device_config_usrptr->id) != 0)
        return -EFAULT;

    // lock global mutex

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // get device wrapper

    device_wrapper = kbdus_control_get_device_wrapper_by_id_(id);

    if (IS_ERR(device_wrapper))
    {
        ret = PTR_ERR(device_wrapper);
        goto out_unlock;
    }

    // copy device configuration to user-space

    if (copy_to_user(
            device_config_usrptr,
            kbdus_device_get_config(device_wrapper->device),
            sizeof(*device_config_usrptr))
        != 0)
    {
        ret = -EFAULT;
        goto out_unlock;
    }

    // success

    ret = 0;

out_unlock:
    mutex_unlock(&kbdus_control_mutex_);
    return ret;
}

static int kbdus_control_ioctl_flush_device_(const u64 __user *id_usrptr)
{
    u64 id;
    struct kbdus_control_device_wrapper_ *device_wrapper;
    struct block_device *bdev;
    int ret;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device id from user space

    if (get_user(id, id_usrptr) != 0)
        return -EFAULT;

    // lock global mutex (so that device isn't destroyed while we are getting
    // the block_device)

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // get device wrapper attached to the device

    device_wrapper = kbdus_control_get_device_wrapper_by_id_(id);

    if (IS_ERR(device_wrapper))
    {
        ret = PTR_ERR(device_wrapper);
        goto out_unlock;
    }

    // skip flush if device is read-only

    if (kbdus_device_is_read_only(device_wrapper->device))
    {
        ret = 0;
        goto out_unlock;
    }

    // get block_device for whole device

    bdev = blkdev_get_by_dev(
        kbdus_device_get_dev(device_wrapper->device), FMODE_READ, NULL);

    if (IS_ERR(bdev))
    {
        ret = PTR_ERR(bdev);
        goto out_unlock;
    }

    // unlock global mutex (to avoid blocking others while we flush)

    mutex_unlock(&kbdus_control_mutex_);

    // flush block_device

    ret = kbdus_control_flush_bdev_(bdev);

    // put block_device

    blkdev_put(bdev, FMODE_READ);

    // return result

    goto out;

out_unlock:
    mutex_unlock(&kbdus_control_mutex_);
out:
    return ret;
}

static int
    kbdus_control_ioctl_trigger_device_destruction_(const u64 __user *id_usrptr)
{
    u64 id;
    struct kbdus_control_device_wrapper_ *device_wrapper;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device id from user space

    if (get_user(id, id_usrptr) != 0)
        return -EFAULT;

    // lock global mutex (so that device isn't destroyed while we are requesting
    // it to terminate)

    if (mutex_lock_interruptible(&kbdus_control_mutex_) != 0)
        return -ERESTARTSYS;

    // get device

    device_wrapper = kbdus_control_get_device_wrapper_by_id_(id);

    if (PTR_ERR_OR_ZERO(device_wrapper) == -EINVAL)
    {
        mutex_unlock(&kbdus_control_mutex_);
        return -EINVAL; // device with given id never existed
    }

    // terminate device (if it still exists) and destroy it if applicable

    if (!IS_ERR(device_wrapper))
    {
        if (device_wrapper->fd)
            kbdus_device_terminate(device_wrapper->device);
        else
            kbdus_control_destroy_device_(device_wrapper);
    }

    // unlock global mutex

    mutex_unlock(&kbdus_control_mutex_);

    // success

    return 0;
}

static int kbdus_control_ioctl_wait_until_device_is_destroyed_(
    const u64 __user *id_usrptr)
{
    u64 id;
    int ret;

    // check capabilities

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    // copy device id from user space

    if (get_user(id, id_usrptr) != 0)
        return -EFAULT;

    // lock wait queue spin lock

    spin_lock(&kbdus_control_device_destroyed_.lock);

    // check if id was ever used

    if (id >= kbdus_control_next_id_)
    {
        // device with given id never existed

        ret = -EINVAL;
    }
    else
    {
        // wait for device to be destroyed (if not already)

        ret = wait_event_interruptible_locked(
            kbdus_control_device_destroyed_,
            IS_ERR(kbdus_control_get_device_wrapper_by_id_(id)));
    }

    // unlock wait queue spin lock

    spin_unlock(&kbdus_control_device_destroyed_.lock);

    // return result

    return ret;
}

static int kbdus_control_ioctl_unknown_(
    struct kbdus_control_fd_ *fd, unsigned int command, unsigned long arg)
{
    // ensure that fd is attached to a device

    if (!test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
        return -ENOTTY;

    // delegate unknown ioctl command to transceiver

    return kbdus_transceiver_handle_control_ioctl(
        fd->transceiver, command, arg);
}

static long kbdus_control_ioctl_(
    struct file *filp, unsigned int command, unsigned long arg)
{
    struct kbdus_control_fd_ *fd;
    void __user *arg_usrptr;

    fd         = filp->private_data;
    arg_usrptr = (void __user *)arg;

    switch (command)
    {
    case KBDUS_IOCTL_GET_VERSION:
        return kbdus_control_ioctl_get_version_(arg_usrptr);

    case KBDUS_IOCTL_CREATE_DEVICE:
        return kbdus_control_ioctl_create_device_(fd, arg_usrptr);

    case KBDUS_IOCTL_ATTACH_TO_DEVICE:
        return kbdus_control_ioctl_attach_to_device_(fd, arg_usrptr);

    case KBDUS_IOCTL_TERMINATE:
        return kbdus_control_ioctl_terminate_(fd);

    case KBDUS_IOCTL_MARK_AS_SUCCESSFUL:
        return kbdus_control_ioctl_mark_as_successful_(fd);

    case KBDUS_IOCTL_DEVICE_PATH_TO_ID:
        return kbdus_control_ioctl_device_path_to_id_(fd, arg_usrptr);

    case KBDUS_IOCTL_GET_DEVICE_CONFIG:
        return kbdus_control_ioctl_get_device_config_(arg_usrptr);

    case KBDUS_IOCTL_FLUSH_DEVICE:
        return kbdus_control_ioctl_flush_device_(arg_usrptr);

    case KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION:
        return kbdus_control_ioctl_trigger_device_destruction_(arg_usrptr);

    case KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED:
        return kbdus_control_ioctl_wait_until_device_is_destroyed_(arg_usrptr);

    default:
        return kbdus_control_ioctl_unknown_(fd, command, arg);
    }
}

static int kbdus_control_mmap_(struct file *filp, struct vm_area_struct *vma)
{
    struct kbdus_control_fd_ *fd;

    fd = filp->private_data;

    // ensure that fd is attached to a device

    if (!test_bit(KBDUS_CONTROL_FD_FLAG_ATTACHED_, &fd->flags))
        return -EINVAL;

    // delegate mmap to transceiver

    return kbdus_transceiver_handle_control_mmap(fd->transceiver, vma);
}

static const struct file_operations kbdus_control_ops_ = {
    .owner          = THIS_MODULE,
    .open           = kbdus_control_open_,
    .release        = kbdus_control_release_,
    .unlocked_ioctl = kbdus_control_ioctl_,
    .compat_ioctl   = kbdus_control_ioctl_,
    .mmap           = kbdus_control_mmap_,
};

/* -------------------------------------------------------------------------- */

int __init kbdus_control_init(void)
{
    int ret;

    BUILD_BUG_ON(sizeof(struct kbdus_version) != 16);
    BUILD_BUG_ON(sizeof(struct kbdus_device_config) != 128);
    BUILD_BUG_ON(sizeof(struct kbdus_fd_config) != 128);
    BUILD_BUG_ON(sizeof(struct kbdus_device_and_fd_config) != 256);

    // ensure that there are enough minor numbers for all devices

    BUILD_BUG_ON((KBDUS_HARD_MAX_DEVICES * DISK_MAX_PARTS) > (1 << MINORBITS));

    // validate module parameter 'max_devices'

    if ((ret = validate_module_parameters_()) != 0)
        goto error;

    // register control

    ret = alloc_chrdev_region(&kbdus_control_devt_, 0, 1, "bdus-control");

    if (ret < 0)
        goto error;

    // initialize and add control device

    cdev_init(&kbdus_control_cdev_, &kbdus_control_ops_);

    ret = cdev_add(&kbdus_control_cdev_, kbdus_control_devt_, 1);

    if (ret < 0)
        goto error_unregister_chrdev_region;

    // create class for control device

    kbdus_control_class_ = class_create(THIS_MODULE, "bdus-control");

    if (IS_ERR(kbdus_control_class_))
    {
        ret = PTR_ERR(kbdus_control_class_);
        goto error_cdev_del;
    }

    // register control device with sysfs

    kbdus_control_device_ = device_create(
        kbdus_control_class_, NULL, kbdus_control_devt_, NULL, "bdus-control");

    if (IS_ERR(kbdus_control_device_))
    {
        ret = PTR_ERR(kbdus_control_device_);
        goto error_class_destroy;
    }

    // success

    return 0;

    // failure

error_class_destroy:
    class_destroy(kbdus_control_class_);
error_cdev_del:
    cdev_del(&kbdus_control_cdev_);
error_unregister_chrdev_region:
    unregister_chrdev_region(kbdus_control_devt_, 1);
error:
    return ret;
}

void kbdus_control_exit(void)
{
    struct kbdus_control_device_wrapper_ *device_wrapper;
    int index;

    // destroy remaining devices

    idr_for_each_entry(&kbdus_control_devices_, device_wrapper, index)
    {
        kbdus_assert(!device_wrapper->fd);

        kbdus_device_destroy(device_wrapper->device);
        kfree(device_wrapper);
    }

    idr_destroy(&kbdus_control_devices_);

    // perform some sanity checks

    kbdus_assert(!mutex_is_locked(&kbdus_control_mutex_));
    kbdus_assert(!spin_is_locked(&kbdus_control_device_destroyed_.lock));
    kbdus_assert(!waitqueue_active(&kbdus_control_device_destroyed_));

    // undo all initialization

    device_destroy(kbdus_control_class_, kbdus_control_devt_);
    class_destroy(kbdus_control_class_);
    cdev_del(&kbdus_control_cdev_);
    unregister_chrdev_region(kbdus_control_devt_, 1);
}

/* -------------------------------------------------------------------------- */
