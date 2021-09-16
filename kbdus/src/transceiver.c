/* SPDX-License-Identifier: GPL-2.0-only */
/* -------------------------------------------------------------------------- */

#include <kbdus.h>
#include <kbdus/config.h>
#include <kbdus/inverter.h>
#include <kbdus/transceiver.h>
#include <kbdus/utilities.h>

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/build_bug.h>
#else
#include <linux/bug.h>
#endif

/* -------------------------------------------------------------------------- */

int __init kbdus_transceiver_init(void)
{
    BUILD_BUG_ON(sizeof(struct kbdus_item) != 64);
    BUILD_BUG_ON(sizeof(struct kbdus_reply) != 64);
    BUILD_BUG_ON(sizeof(union kbdus_reply_or_item) != 64);

    return 0;
}

void kbdus_transceiver_exit(void)
{
}

/* -------------------------------------------------------------------------- */

struct kbdus_transceiver
{
    struct kbdus_inverter *inverter;

    u32 num_rais;
    u32 num_preallocated_buffers;
    size_t preallocated_buffer_size;

    void *shared_memory;
    void *preallocated_buffers_start;
};

/* -------------------------------------------------------------------------- */

static size_t kbdus_transceiver_get_max_request_size_(
    const struct kbdus_device_config *device_config)
{
    size_t size;

    size = (size_t)device_config->max_read_write_size;

    if (device_config->supports_write_same)
        size = max(size, (size_t)device_config->logical_block_size);

    if (device_config->supports_ioctl)
        size = max(size, (size_t)1 << 14);

    return size;
}

// Returns NULL if invalid rai_index.
static union kbdus_reply_or_item *kbdus_transceiver_get_rai_(
    const struct kbdus_transceiver *transceiver, u64 rai_index)
{
    if (rai_index >= (u64)transceiver->num_rais)
        return NULL;

    return transceiver->shared_memory + 64 * rai_index;
}

// Returns NULL if invalid preallocated_buffer_index.
static void *kbdus_transceiver_get_preallocated_buffer_(
    const struct kbdus_transceiver *transceiver, u64 preallocated_buffer_index)
{
    if (preallocated_buffer_index >= (u64)transceiver->num_preallocated_buffers)
        return NULL;

    return transceiver->preallocated_buffers_start
        + transceiver->preallocated_buffer_size * preallocated_buffer_index;
}

/* -------------------------------------------------------------------------- */

static int kbdus_transceiver_copy_req_to_item_and_user_buffer_(
    const struct kbdus_transceiver *transceiver,
    const struct kbdus_inverter_item *inverter_item, struct kbdus_item *item)
{
    void __user *payload_buffer_usrptr;
    struct bio_vec bvec;
    struct req_iterator req_iter;
    void *bvec_mapped_page;
    int ret;
    struct kbdus_inverter_pdu *pdu;

    payload_buffer_usrptr = (void __user *)item->user_ptr_or_buffer_index;

    switch (inverter_item->type)
    {
    case KBDUS_ITEM_TYPE_WRITE:
    case KBDUS_ITEM_TYPE_WRITE_SAME:
    case KBDUS_ITEM_TYPE_FUA_WRITE:

        if (!kbdus_access_ok(
                KBDUS_VERIFY_WRITE, payload_buffer_usrptr,
                (size_t)blk_rq_bytes(inverter_item->req)))
        {
            return -EFAULT;
        }

        rq_for_each_segment(bvec, inverter_item->req, req_iter)
        {
            bvec_mapped_page = kmap(bvec.bv_page);

            ret = __copy_to_user(
                payload_buffer_usrptr, bvec_mapped_page + bvec.bv_offset,
                bvec.bv_len);

            kunmap(bvec_mapped_page);

            if (ret != 0)
                return -EFAULT;

            payload_buffer_usrptr += bvec.bv_len;
        }

        /* fallthrough */

    case KBDUS_ITEM_TYPE_READ:
    case KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP:
    case KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP:
    case KBDUS_ITEM_TYPE_DISCARD:
    case KBDUS_ITEM_TYPE_SECURE_ERASE:

        item->arg64 = 512ull * (u64)blk_rq_pos(inverter_item->req);
        item->arg32 = (u32)blk_rq_bytes(inverter_item->req);

        break;

    case KBDUS_ITEM_TYPE_IOCTL:

        pdu = blk_mq_rq_to_pdu(inverter_item->req);

        item->arg32 = (u32)pdu->ioctl_command;

        if (_IOC_DIR(pdu->ioctl_command) & _IOC_READ)
        {
            if (copy_to_user(
                    payload_buffer_usrptr, pdu->ioctl_argument,
                    (size_t)_IOC_SIZE(pdu->ioctl_command))
                != 0)
            {
                return -EFAULT;
            }
        }

        break;
    }

    return 0;
}

static int kbdus_transceiver_copy_req_to_item_and_preallocated_buffer_(
    const struct kbdus_transceiver *transceiver,
    const struct kbdus_inverter_item *inverter_item, struct kbdus_item *item)
{
    void *payload_buffer;
    struct bio_vec bvec;
    struct req_iterator req_iter;
    void *bvec_mapped_page;
    struct kbdus_inverter_pdu *pdu;

    payload_buffer = kbdus_transceiver_get_preallocated_buffer_(
        transceiver, item->user_ptr_or_buffer_index);

    if (!payload_buffer)
        return -EINVAL;

    switch (inverter_item->type)
    {
    case KBDUS_ITEM_TYPE_WRITE:
    case KBDUS_ITEM_TYPE_WRITE_SAME:
    case KBDUS_ITEM_TYPE_FUA_WRITE:

        rq_for_each_segment(bvec, inverter_item->req, req_iter)
        {
            bvec_mapped_page = kmap(bvec.bv_page);

            memcpy(
                payload_buffer, bvec_mapped_page + bvec.bv_offset, bvec.bv_len);

            kunmap(bvec_mapped_page);

            payload_buffer += bvec.bv_len;
        }

        /* fallthrough */

    case KBDUS_ITEM_TYPE_READ:
    case KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP:
    case KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP:
    case KBDUS_ITEM_TYPE_DISCARD:
    case KBDUS_ITEM_TYPE_SECURE_ERASE:

        item->arg64 = 512ull * (u64)blk_rq_pos(inverter_item->req);
        item->arg32 = (u32)blk_rq_bytes(inverter_item->req);

        break;

    case KBDUS_ITEM_TYPE_IOCTL:

        pdu = blk_mq_rq_to_pdu(inverter_item->req);

        item->arg32 = (u32)pdu->ioctl_command;

        if (_IOC_DIR(pdu->ioctl_command) & _IOC_READ)
        {
            memcpy(
                payload_buffer, pdu->ioctl_argument,
                (size_t)_IOC_SIZE(pdu->ioctl_command));
        }

        break;
    }

    return 0;
}

static int kbdus_transceiver_copy_reply_data_from_user_buffer_(
    const struct kbdus_transceiver *transceiver,
    const struct kbdus_inverter_item *inverter_item,
    const void __user *payload_buffer_usrptr)
{
    struct bio_vec bvec;
    struct req_iterator req_iter;
    void *bvec_mapped_page;
    int ret;
    struct kbdus_inverter_pdu *pdu;

    switch (inverter_item->type)
    {
    case KBDUS_ITEM_TYPE_READ:

        if (!kbdus_access_ok(
                KBDUS_VERIFY_READ, payload_buffer_usrptr,
                (size_t)blk_rq_bytes(inverter_item->req)))
        {
            return -EFAULT;
        }

        rq_for_each_segment(bvec, inverter_item->req, req_iter)
        {
            bvec_mapped_page = kmap(bvec.bv_page);

            ret = __copy_from_user(
                bvec_mapped_page + bvec.bv_offset, payload_buffer_usrptr,
                bvec.bv_len);

            kunmap(bvec_mapped_page);

            if (ret != 0)
                return -EFAULT;

            payload_buffer_usrptr += bvec.bv_len;
        }

        break;

    case KBDUS_ITEM_TYPE_IOCTL:

        pdu = blk_mq_rq_to_pdu(inverter_item->req);

        if (_IOC_DIR(pdu->ioctl_command) & _IOC_WRITE)
        {
            if (copy_from_user(
                    pdu->ioctl_argument, payload_buffer_usrptr,
                    (size_t)_IOC_SIZE(pdu->ioctl_command))
                != 0)
            {
                return -EFAULT;
            }
        }

        break;
    }

    return 0;
}

static int kbdus_transceiver_copy_reply_data_from_preallocated_buffer_(
    const struct kbdus_transceiver *transceiver,
    const struct kbdus_inverter_item *inverter_item,
    u64 preallocated_buffer_index)
{
    void *payload_buffer;
    struct bio_vec bvec;
    struct req_iterator req_iter;
    void *bvec_mapped_page;
    struct kbdus_inverter_pdu *pdu;

    payload_buffer = kbdus_transceiver_get_preallocated_buffer_(
        transceiver, preallocated_buffer_index);

    if (!payload_buffer)
        return -EINVAL;

    switch (inverter_item->type)
    {
    case KBDUS_ITEM_TYPE_READ:

        rq_for_each_segment(bvec, inverter_item->req, req_iter)
        {
            bvec_mapped_page = kmap(bvec.bv_page);

            memcpy(
                bvec_mapped_page + bvec.bv_offset, payload_buffer, bvec.bv_len);

            kunmap(bvec_mapped_page);

            payload_buffer += bvec.bv_len;
        }

        break;

    case KBDUS_ITEM_TYPE_IOCTL:

        pdu = blk_mq_rq_to_pdu(inverter_item->req);

        if (_IOC_DIR(pdu->ioctl_command) & _IOC_WRITE)
        {
            memcpy(
                pdu->ioctl_argument, payload_buffer,
                (size_t)_IOC_SIZE(pdu->ioctl_command));
        }

        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

static int kbdus_transceiver_receive_item_(
    const struct kbdus_transceiver *transceiver, struct kbdus_item *item)
{
    const struct kbdus_inverter_item *inverter_item;
    int ret;

    // get request to be processed

    inverter_item = kbdus_inverter_begin_item_get(transceiver->inverter);

    if (IS_ERR(inverter_item))
        return PTR_ERR(inverter_item);

    // put item into the rai's item structure

    item->handle_seqnum = inverter_item->handle_seqnum;
    item->handle_index  = inverter_item->handle_index;
    item->type          = inverter_item->type;

    // put request data into the payload buffer

    if (!item->use_preallocated_buffer)
    {
        ret = kbdus_transceiver_copy_req_to_item_and_user_buffer_(
            transceiver, inverter_item, item);
    }
    else
    {
        ret = kbdus_transceiver_copy_req_to_item_and_preallocated_buffer_(
            transceiver, inverter_item, item);
    }

    // commit or abort "request get"

    if (ret == 0)
        kbdus_inverter_commit_item_get(transceiver->inverter, inverter_item);
    else
        kbdus_inverter_abort_item_get(transceiver->inverter, inverter_item);

    // return result

    return ret;
}

static int kbdus_transceiver_send_reply_(
    const struct kbdus_transceiver *transceiver,
    const struct kbdus_reply *reply)
{
    const struct kbdus_inverter_item *inverter_item;
    int ret;

    // check if there is a reply at all

    if (reply->handle_index == 0)
        return 0;

    // get inverter request

    inverter_item = kbdus_inverter_begin_item_completion(
        transceiver->inverter, reply->handle_index, reply->handle_seqnum);

    if (!inverter_item)
        return 0; // request timed out, was canceled, or was already completed

    if (IS_ERR(inverter_item))
        return PTR_ERR(inverter_item);

    // copy data if applicable and request succeeded

    ret = 0;

    if (reply->error == 0)
    {
        if (!reply->use_preallocated_buffer)
        {
            ret = kbdus_transceiver_copy_reply_data_from_user_buffer_(
                transceiver, inverter_item,
                (const void __user *)reply->user_ptr_or_buffer_index);
        }
        else
        {
            ret = kbdus_transceiver_copy_reply_data_from_preallocated_buffer_(
                transceiver, inverter_item, reply->user_ptr_or_buffer_index);
        }
    }

    // commit or abort request completion

    if (ret == 0)
    {
        kbdus_inverter_commit_item_completion(
            transceiver->inverter, inverter_item, (int)(-(reply->error)));
    }
    else
    {
        kbdus_inverter_abort_item_completion(
            transceiver->inverter, inverter_item);
    }

    // return result

    return ret;
}

static int kbdus_transceiver_send_reply_and_receive_item_(
    const struct kbdus_transceiver *transceiver, union kbdus_reply_or_item *rai)
{
    int ret;

    ret = kbdus_transceiver_send_reply_(transceiver, &rai->reply);

    if (ret == 0)
        ret = kbdus_transceiver_receive_item_(transceiver, &rai->item);

    return ret;
}

/* -------------------------------------------------------------------------- */

int kbdus_transceiver_validate_and_adjust_config(
    struct kbdus_device_and_fd_config *config)
{
    // ensure that reserved space is zeroed out

    if (!kbdus_array_is_zero_filled(config->fd.reserved_))
        return -EINVAL;

    // adjust configuration

    config->fd.num_preallocated_buffers =
        min(config->fd.num_preallocated_buffers,
            config->device.max_outstanding_reqs);

    return 0;
}

struct kbdus_transceiver *kbdus_transceiver_create(
    const struct kbdus_device_and_fd_config *config,
    struct kbdus_inverter *inverter)
{
    struct kbdus_transceiver *transceiver;

    // allocate transceiver struct

    transceiver = kzalloc(sizeof(*transceiver), GFP_KERNEL);

    if (!transceiver)
        return ERR_PTR(-ENOMEM);

    transceiver->inverter = inverter;

    transceiver->num_rais                 = config->device.max_outstanding_reqs;
    transceiver->num_preallocated_buffers = config->fd.num_preallocated_buffers;
    transceiver->preallocated_buffer_size =
        PAGE_ALIGN(kbdus_transceiver_get_max_request_size_(&config->device));

    // allocate shared memory (RAIs + preallocated buffers)

    transceiver->shared_memory = vmalloc_user(
        PAGE_ALIGN(transceiver->num_rais * 64)
        + transceiver->num_preallocated_buffers
            * transceiver->preallocated_buffer_size);

    if (!transceiver->shared_memory)
    {
        kfree(transceiver);
        return ERR_PTR(-ENOMEM);
    }

    transceiver->preallocated_buffers_start =
        transceiver->shared_memory + PAGE_ALIGN(transceiver->num_rais * 64);

    // success

    return transceiver;
}

void kbdus_transceiver_destroy(struct kbdus_transceiver *transceiver)
{
    vfree(transceiver->shared_memory);
    kfree(transceiver);
}

int kbdus_transceiver_handle_control_ioctl(
    struct kbdus_transceiver *transceiver, unsigned int command,
    unsigned long argument)
{
    union kbdus_reply_or_item *rai;

    // validate command

    switch (command)
    {
    case KBDUS_IOCTL_RECEIVE_ITEM:
    case KBDUS_IOCTL_SEND_REPLY:
    case KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM:
        break;

    default:
        return -ENOTTY;
    }

    // get rai

    rai = kbdus_transceiver_get_rai_(transceiver, (u64)argument);

    if (!rai)
        return -EINVAL;

    // delegate processing to appropriate function

    switch (command)
    {
    case KBDUS_IOCTL_RECEIVE_ITEM:
        return kbdus_transceiver_receive_item_(transceiver, &rai->item);

    case KBDUS_IOCTL_SEND_REPLY:
        return kbdus_transceiver_send_reply_(transceiver, &rai->reply);

    case KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM:
        return kbdus_transceiver_send_reply_and_receive_item_(transceiver, rai);

    default:
        return -ENOTTY;
    }
}

int kbdus_transceiver_handle_control_mmap(
    struct kbdus_transceiver *transceiver, struct vm_area_struct *vma)
{
    return remap_vmalloc_range(vma, transceiver->shared_memory, vma->vm_pgoff);
}

/* -------------------------------------------------------------------------- */
