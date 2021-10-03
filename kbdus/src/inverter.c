/* SPDX-License-Identifier: GPL-2.0-only */
/* -------------------------------------------------------------------------- */

#include <kbdus/inverter.h>
#include <kbdus/utilities.h>

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/wait.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/build_bug.h>
#else
#include <linux/bug.h>
#endif

/* -------------------------------------------------------------------------- */

enum
{
    KBDUS_INVERTER_FLAG_DEACTIVATED_             = 1u << 0,
    KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_ = 1u << 1,
    KBDUS_INVERTER_FLAG_TERMINATED_              = 1u << 2,
    KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_   = 1u << 3,

    KBDUS_INVERTER_FLAG_SUPPORTS_READ_  = 1u << 4,
    KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_ = 1u << 5,
    KBDUS_INVERTER_FLAG_SUPPORTS_FLUSH_ = 1u << 6,
    KBDUS_INVERTER_FLAG_SUPPORTS_IOCTL_ = 1u << 7,

#if KBDUS_DEBUG

    KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_SAME_   = 1u << 8,
    KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_ZEROS_  = 1u << 9,
    KBDUS_INVERTER_FLAG_SUPPORTS_FUA_WRITE_    = 1u << 10,
    KBDUS_INVERTER_FLAG_SUPPORTS_DISCARD_      = 1u << 11,
    KBDUS_INVERTER_FLAG_SUPPORTS_SECURE_ERASE_ = 1u << 12,

#endif
};

enum
{
    // request wrapper is not holding a request
    KBDUS_REQ_STATE_FREE_,

    // request wrapper is awaiting to be gotten by
    // `kbdus_inverter_begin_item_get()`
    KBDUS_REQ_STATE_AWAITING_GET_,

    // request wrapper is being gotten, i.e.,
    // `kbdus_inverter_begin_item_get()` has been called but
    // `kbdus_inverter_commit_item_get()` or
    // `kbdus_inverter_abort_item_get()` have not
    KBDUS_REQ_STATE_BEING_GOTTEN_,

    // request structure has been gotten and is awaiting to be completed by
    // `kbdus_inverter_begin_item_completion()`
    KBDUS_REQ_STATE_AWAITING_COMPLETION_,

    // request wrapper is being completed, i.e.,
    // `kbdus_inverter_begin_item_completion()` has been called but
    // `kbdus_inverter_commit_item_completion()` or
    // `kbdus_inverter_abort_item_completion()` have not
    KBDUS_REQ_STATE_BEING_COMPLETED_,
};

static const struct kbdus_inverter_item kbdus_inverter_req_dev_available_ = {
    .handle_index  = 0,
    .handle_seqnum = 0,
    .type          = KBDUS_ITEM_TYPE_DEVICE_AVAILABLE,
};

static const struct kbdus_inverter_item kbdus_inverter_req_terminate_ = {
    .handle_index  = 0,
    .handle_seqnum = 0,
    .type          = KBDUS_ITEM_TYPE_TERMINATE,
};

static const struct kbdus_inverter_item kbdus_inverter_req_flush_terminate_ = {
    .handle_index  = 0,
    .handle_seqnum = 0,
    .type          = KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE,
};

/* -------------------------------------------------------------------------- */

struct kbdus_inverter_req_wrapper_
{
    // - 32-bit archs: bytes 0-7
    // - 64-bit archs: bytes 0-15
    struct list_head list;

    // - 32-bit archs: bytes 8-23
    // - 64-bit archs: bytes 16-39
    struct kbdus_inverter_item item;

    // - 32-bit archs: bytes 24-27
    // - 64-bit archs: bytes 40-43
    u32 state;

    // pad to 64 bytes
#if BITS_PER_LONG == 32
    u8 padding_[36];
#else
    u8 padding_[20];
#endif
};

struct kbdus_inverter
{
    u32 flags;
    u32 num_reqs;

    struct completion item_is_awaiting_get;

    spinlock_t reqs_lock;
    struct list_head reqs_free;
    struct list_head reqs_awaiting_get;

    void *reqs_all_unaligned;
    struct kbdus_inverter_req_wrapper_ *reqs_all;
};

/* -------------------------------------------------------------------------- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
#define KBDUS_INVERTER_BLK_EH_DONE_ BLK_EH_DONE
#else
#define KBDUS_INVERTER_BLK_EH_DONE_ BLK_EH_NOT_HANDLED
#endif

/* -------------------------------------------------------------------------- */

static enum kbdus_item_type
    kbdus_inverter_req_to_item_type_(const struct request *req)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)

    switch (req_op(req))
    {
    case REQ_OP_READ:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
        return KBDUS_ITEM_TYPE_READ;
#else
        if (req->cmd_type == REQ_TYPE_DRV_PRIV)
            return KBDUS_ITEM_TYPE_IOCTL;
        else
            return KBDUS_ITEM_TYPE_READ;
#endif

    case REQ_OP_WRITE:
        if (req->cmd_flags & REQ_FUA)
            return KBDUS_ITEM_TYPE_FUA_WRITE;
        else
            return KBDUS_ITEM_TYPE_WRITE;

    case REQ_OP_WRITE_SAME:
        return KBDUS_ITEM_TYPE_WRITE_SAME;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
    case REQ_OP_WRITE_ZEROES:
        if (req->cmd_flags & REQ_NOUNMAP)
            return KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP;
        else
            return KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
    case REQ_OP_WRITE_ZEROES:
        return KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP;
#endif

    case REQ_OP_FLUSH:
        return KBDUS_ITEM_TYPE_FLUSH;

    case REQ_OP_DISCARD:
        return KBDUS_ITEM_TYPE_DISCARD;

    case REQ_OP_SECURE_ERASE:
        return KBDUS_ITEM_TYPE_SECURE_ERASE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    case REQ_OP_DRV_IN:
        return KBDUS_ITEM_TYPE_IOCTL;
#endif

    default:
        WARN_ON(true);
        return -1;
    }

#else

    if ((req->cmd_flags & (REQ_DISCARD | REQ_SECURE))
        == (REQ_DISCARD | REQ_SECURE))
        return KBDUS_ITEM_TYPE_SECURE_ERASE;

    if (req->cmd_flags & REQ_DISCARD)
        return KBDUS_ITEM_TYPE_DISCARD;

    if (req->cmd_flags & REQ_FLUSH)
        return KBDUS_ITEM_TYPE_FLUSH;

    if (req->cmd_flags & REQ_WRITE_SAME)
        return KBDUS_ITEM_TYPE_WRITE_SAME;

    if ((req->cmd_flags & (REQ_WRITE | REQ_FUA)) == (REQ_WRITE | REQ_FUA))
        return KBDUS_ITEM_TYPE_FUA_WRITE;

    if (req->cmd_flags & REQ_WRITE)
        return KBDUS_ITEM_TYPE_WRITE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
    if (req->cmd_type == REQ_TYPE_DRV_PRIV)
        return KBDUS_ITEM_TYPE_IOCTL;
#else
    if (req->cmd_type == REQ_TYPE_SPECIAL)
        return KBDUS_ITEM_TYPE_IOCTL;
#endif

    return KBDUS_ITEM_TYPE_READ;

#endif
}

static bool kbdus_inverter_req_is_supported_(
    const struct kbdus_inverter *inverter, enum kbdus_item_type item_type)
{
    switch (item_type)
    {
        // these three request types can always end up in the request queue

    case KBDUS_ITEM_TYPE_READ:
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_READ_;

    case KBDUS_ITEM_TYPE_WRITE:
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_;

    case KBDUS_ITEM_TYPE_IOCTL:
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_IOCTL_;

#if KBDUS_DEBUG

        // but the following should only appear if they are explicitly enabled

    case KBDUS_ITEM_TYPE_WRITE_SAME:
        WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_SAME_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_SAME_;

    case KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP:
    case KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP:
        WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_ZEROS_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_ZEROS_;

    case KBDUS_ITEM_TYPE_FUA_WRITE:
        WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_FUA_WRITE_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_FUA_WRITE_;

    case KBDUS_ITEM_TYPE_FLUSH:
        WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_FLUSH_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_FLUSH_;

    case KBDUS_ITEM_TYPE_DISCARD:
        WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_DISCARD_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_DISCARD_;

    case KBDUS_ITEM_TYPE_SECURE_ERASE:
        WARN_ON(
            !(inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_SECURE_ERASE_));
        return inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_SECURE_ERASE_;

        // and there aren't any more request types

    default:
        WARN_ON(true);
        return false;

#else

    default:
        return true;

#endif
    }
}

static struct kbdus_inverter_req_wrapper_ *
    kbdus_inverter_handle_index_to_wrapper_(
        struct kbdus_inverter *inverter, u16 index)
{
    index -= 1;

    if ((u32)index >= inverter->num_reqs)
        return NULL;

    return &inverter->reqs_all[index];
}

/* -------------------------------------------------------------------------- */

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_to_awaiting_get_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper)
{
#if KBDUS_DEBUG
    WARN_ON(
        wrapper->state != KBDUS_REQ_STATE_FREE_
        && wrapper->state != KBDUS_REQ_STATE_AWAITING_COMPLETION_
        && wrapper->state != KBDUS_REQ_STATE_BEING_GOTTEN_);
#endif

    // put wrapper into "awaiting get" list

    if (wrapper->state == KBDUS_REQ_STATE_FREE_)
        list_move_tail(&wrapper->list, &inverter->reqs_awaiting_get);
    else
        list_add(&wrapper->list, &inverter->reqs_awaiting_get);

    // set wrapper state

    wrapper->state = KBDUS_REQ_STATE_AWAITING_GET_;

    // notify single waiter that item is awaiting get

    complete(&inverter->item_is_awaiting_get);
}

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_to_being_gotten_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper)
{
#if KBDUS_DEBUG
    WARN_ON(wrapper->state != KBDUS_REQ_STATE_AWAITING_GET_);
#endif

    list_del(&wrapper->list);

    wrapper->state = KBDUS_REQ_STATE_BEING_GOTTEN_;
}

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_to_awaiting_completion_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper)
{
#if KBDUS_DEBUG
    WARN_ON(
        wrapper->state != KBDUS_REQ_STATE_BEING_GOTTEN_
        && wrapper->state != KBDUS_REQ_STATE_BEING_COMPLETED_);
#endif

    wrapper->state = KBDUS_REQ_STATE_AWAITING_COMPLETION_;
}

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_to_being_completed_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper)
{
#if KBDUS_DEBUG
    WARN_ON(wrapper->state != KBDUS_REQ_STATE_AWAITING_COMPLETION_);
#endif

    wrapper->state = KBDUS_REQ_STATE_BEING_COMPLETED_;
}

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_to_free_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper, int neg_errno,
    int neg_errno_ioctl)
{
    struct kbdus_inverter_pdu *pdu;

#if KBDUS_DEBUG
    WARN_ON(
        wrapper->state != KBDUS_REQ_STATE_AWAITING_GET_
        && wrapper->state != KBDUS_REQ_STATE_BEING_GOTTEN_
        && wrapper->state != KBDUS_REQ_STATE_AWAITING_COMPLETION_
        && wrapper->state != KBDUS_REQ_STATE_BEING_COMPLETED_);
#endif

    // complete request

    pdu = blk_mq_rq_to_pdu(wrapper->item.req);

    pdu->error       = neg_errno;
    pdu->error_ioctl = neg_errno_ioctl;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
    blk_mq_complete_request(wrapper->item.req);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0)
    blk_mq_complete_request(wrapper->item.req, neg_errno);
#else
    wrapper->item.req->errors = neg_errno;
    blk_mq_complete_request(wrapper->item.req);
#endif

    // increment wrapper seqnum

    wrapper->item.handle_seqnum += 1;

    // put wrapper into "awaiting get" list

    if (wrapper->state == KBDUS_REQ_STATE_AWAITING_GET_)
        list_move(&wrapper->list, &inverter->reqs_free);
    else
        list_add(&wrapper->list, &inverter->reqs_free);

    // set wrapper state

    wrapper->state = KBDUS_REQ_STATE_FREE_;
}

// Must be called with `inverter->reqs_lock` held.
static void kbdus_inverter_wrapper_cancel_due_to_termination_(
    struct kbdus_inverter *inverter,
    struct kbdus_inverter_req_wrapper_ *wrapper)
{
    kbdus_inverter_wrapper_to_free_(inverter, wrapper, -EIO, -ENODEV);
}

/* -------------------------------------------------------------------------- */

int __init kbdus_inverter_init(void)
{
    BUILD_BUG_ON(sizeof(struct kbdus_inverter_req_wrapper_) != 64);

    return 0;
}

void kbdus_inverter_exit(void)
{
}

/* -------------------------------------------------------------------------- */

struct kbdus_inverter *
    kbdus_inverter_create(const struct kbdus_device_config *device_config)
{
    struct kbdus_inverter *inverter;
    u32 i;

    // allocate inverter

    inverter = kzalloc(sizeof(*inverter), GFP_KERNEL);

    if (!inverter)
        return ERR_PTR(-ENOMEM);

    // allocate request wrappers

    inverter->reqs_all_unaligned =
        kzalloc(64 * device_config->max_outstanding_reqs + 63, GFP_KERNEL);

    if (!inverter->reqs_all_unaligned)
    {
        kfree(inverter);
        return ERR_PTR(-ENOMEM);
    }

    inverter->reqs_all = PTR_ALIGN(inverter->reqs_all_unaligned, 64);

    // initialize inverter structure

    inverter->flags = 0;

    if (device_config->supports_read)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_READ_;

    if (device_config->supports_write)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_;

    if (device_config->supports_flush)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_FLUSH_;

    if (device_config->supports_ioctl)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_IOCTL_;

#if KBDUS_DEBUG

    if (device_config->supports_write_same)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_SAME_;

    if (device_config->supports_write_zeros)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_WRITE_ZEROS_;

    if (device_config->supports_fua_write)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_FUA_WRITE_;

    if (device_config->supports_discard)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_DISCARD_;

    if (device_config->supports_secure_erase)
        inverter->flags |= KBDUS_INVERTER_FLAG_SUPPORTS_SECURE_ERASE_;

#endif

    inverter->num_reqs = device_config->max_outstanding_reqs;

    INIT_LIST_HEAD(&inverter->reqs_free);
    INIT_LIST_HEAD(&inverter->reqs_awaiting_get);

    spin_lock_init(&inverter->reqs_lock);
    init_completion(&inverter->item_is_awaiting_get);

    for (i = 0; i < device_config->max_outstanding_reqs; ++i)
    {
        inverter->reqs_all[i].state              = KBDUS_REQ_STATE_FREE_;
        inverter->reqs_all[i].item.handle_index  = (u16)(i + 1);
        inverter->reqs_all[i].item.handle_seqnum = 0;

        list_add_tail(&inverter->reqs_all[i].list, &inverter->reqs_free);
    }

    // success

    return inverter;
}

void kbdus_inverter_destroy(struct kbdus_inverter *inverter)
{
    u32 i;

    // perform some sanity checks

    WARN_ON(!(inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_));

    WARN_ON(kbdus_list_length(&inverter->reqs_free) != inverter->num_reqs);
    WARN_ON(!list_empty(&inverter->reqs_awaiting_get));

    for (i = 0; i < inverter->num_reqs; ++i)
        WARN_ON(inverter->reqs_all[i].state != KBDUS_REQ_STATE_FREE_);

    WARN_ON(spin_is_locked(&inverter->reqs_lock));

    // free inverter structure

    kfree(inverter->reqs_all_unaligned);
    kfree(inverter);
}

void kbdus_inverter_terminate(struct kbdus_inverter *inverter)
{
    u32 i;
    struct kbdus_inverter_req_wrapper_ *wrapper;

    spin_lock_irq(&inverter->reqs_lock);

    if (!(inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_))
    {
        inverter->flags |= KBDUS_INVERTER_FLAG_TERMINATED_;

        // fail requests awaiting get or awaiting completion

        for (i = 0; i < inverter->num_reqs; ++i)
        {
            wrapper = &inverter->reqs_all[i];

            if (wrapper->state == KBDUS_REQ_STATE_AWAITING_GET_
                || wrapper->state == KBDUS_REQ_STATE_AWAITING_COMPLETION_)
            {
                kbdus_inverter_wrapper_cancel_due_to_termination_(
                    inverter, wrapper);
            }
        }

        // notify waiters of infinite termination requests
        complete_all(&inverter->item_is_awaiting_get);
    }

    spin_unlock_irq(&inverter->reqs_lock);
}

void kbdus_inverter_deactivate(struct kbdus_inverter *inverter, bool flush)
{
    spin_lock_irq(&inverter->reqs_lock);

    WARN_ON(inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_);

    if (!(inverter->flags & KBDUS_INVERTER_FLAG_DEACTIVATED_))
    {
        inverter->flags |= KBDUS_INVERTER_FLAG_DEACTIVATED_;

        if (flush && (inverter->flags & KBDUS_INVERTER_FLAG_SUPPORTS_FLUSH_))
            inverter->flags |= KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_;
    }

    // notify waiters of infinite termination requests
    complete_all(&inverter->item_is_awaiting_get);

    spin_unlock_irq(&inverter->reqs_lock);
}

void kbdus_inverter_activate(struct kbdus_inverter *inverter)
{
    u32 i;
    struct kbdus_inverter_req_wrapper_ *wrapper;

    spin_lock_irq(&inverter->reqs_lock);

    WARN_ON(inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_);

    // test and clear "deactivated" flag

    if (inverter->flags & KBDUS_INVERTER_FLAG_DEACTIVATED_)
    {
        inverter->flags &=
            ~(KBDUS_INVERTER_FLAG_DEACTIVATED_
              | KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_);

        // reinitialize completion

        reinit_completion(&inverter->item_is_awaiting_get);

        // move all requests with state AWAITING_COMPLETION back to AWAITING_GET

        for (i = 0; i < inverter->num_reqs; ++i)
        {
            wrapper = &inverter->reqs_all[i];

            WARN_ON(
                wrapper->state != KBDUS_REQ_STATE_FREE_
                && wrapper->state != KBDUS_REQ_STATE_AWAITING_GET_
                && wrapper->state != KBDUS_REQ_STATE_AWAITING_COMPLETION_);

            switch (wrapper->state)
            {
            case KBDUS_REQ_STATE_AWAITING_GET_:
                complete(&inverter->item_is_awaiting_get);
                break;

            case KBDUS_REQ_STATE_AWAITING_COMPLETION_:
                kbdus_inverter_wrapper_to_awaiting_get_(inverter, wrapper);
                break;
            }
        }

        // notify waiters of "device available" request if appropriate

        if (inverter->flags & KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_)
            complete(&inverter->item_is_awaiting_get);
    }

    // unlock request lock

    spin_unlock_irq(&inverter->reqs_lock);
}

void kbdus_inverter_submit_device_available_notification(
    struct kbdus_inverter *inverter)
{
    spin_lock_irq(&inverter->reqs_lock);

    if (!(inverter->flags & KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_))
    {
        inverter->flags |= KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_;
        complete(&inverter->item_is_awaiting_get);
    }

    spin_unlock_irq(&inverter->reqs_lock);
}

int kbdus_inverter_submit_request(
    struct kbdus_inverter *inverter, struct request *req)
{
    struct kbdus_inverter_pdu *pdu;
    enum kbdus_item_type item_type;
    unsigned long flags;
    struct kbdus_inverter_req_wrapper_ *wrapper;

    pdu       = blk_mq_rq_to_pdu(req);
    item_type = kbdus_inverter_req_to_item_type_(req);

    // lock request lock

    spin_lock_irqsave(&inverter->reqs_lock, flags);

    // perform some sanity checks

#if KBDUS_DEBUG
    WARN_ON(list_empty(&inverter->reqs_free));
#endif

    // fail request if inverter was terminated

    if (inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_)
    {
        spin_unlock_irqrestore(&inverter->reqs_lock, flags);

        pdu->handle_index  = 0;
        pdu->handle_seqnum = 0;

        pdu->error       = -EIO;
        pdu->error_ioctl = -ENODEV;

        return -EIO;
    }

    // reject read, write, or ioctl if unsupported

    if (!kbdus_inverter_req_is_supported_(inverter, item_type))
    {
        spin_unlock_irqrestore(&inverter->reqs_lock, flags);

        pdu->handle_index  = 0;
        pdu->handle_seqnum = 0;

        pdu->error       = -EOPNOTSUPP;
        pdu->error_ioctl = -ENOTTY;

        return -EOPNOTSUPP;
    }

    // get free request wrapper

    wrapper = list_first_entry(
        &inverter->reqs_free, struct kbdus_inverter_req_wrapper_, list);

    // initialize request

    wrapper->item.type = item_type;
    wrapper->item.req  = req;

    // set request state and move wrapper to appropriate list

    kbdus_inverter_wrapper_to_awaiting_get_(inverter, wrapper);

    // start request

    blk_mq_start_request(req);

    // unlock request lock

    spin_unlock_irqrestore(&inverter->reqs_lock, flags);

    // return request handle

    pdu->handle_index  = wrapper->item.handle_index;
    pdu->handle_seqnum = wrapper->item.handle_seqnum;

    // success

    return 0;
}

enum blk_eh_timer_return kbdus_inverter_timeout_request(
    struct kbdus_inverter *inverter, struct request *req)
{
    struct kbdus_inverter_pdu *pdu;
    struct kbdus_inverter_req_wrapper_ *wrapper;
    unsigned long flags;
    int ret;

    pdu = blk_mq_rq_to_pdu(req);

    // get wrapper

    wrapper =
        kbdus_inverter_handle_index_to_wrapper_(inverter, pdu->handle_index);

    // lock request lock

    spin_lock_irqsave(&inverter->reqs_lock, flags);

    // perform some sanity checks

    WARN_ON(!wrapper);

    WARN_ON(
        wrapper->item.type != KBDUS_ITEM_TYPE_READ
        && wrapper->item.type != KBDUS_ITEM_TYPE_WRITE
        && wrapper->item.type != KBDUS_ITEM_TYPE_WRITE_SAME
        && wrapper->item.type != KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP
        && wrapper->item.type != KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP
        && wrapper->item.type != KBDUS_ITEM_TYPE_FUA_WRITE
        && wrapper->item.type != KBDUS_ITEM_TYPE_FLUSH
        && wrapper->item.type != KBDUS_ITEM_TYPE_DISCARD
        && wrapper->item.type != KBDUS_ITEM_TYPE_SECURE_ERASE
        && wrapper->item.type != KBDUS_ITEM_TYPE_IOCTL);

    if (wrapper->item.handle_seqnum != pdu->handle_seqnum)
    {
        ret = KBDUS_INVERTER_BLK_EH_DONE_;
    }
    else
    {
        switch (wrapper->state)
        {
        case KBDUS_REQ_STATE_BEING_GOTTEN_:
        case KBDUS_REQ_STATE_BEING_COMPLETED_:

            // can't timeout requests in these states, restart timer

            ret = BLK_EH_RESET_TIMER;

            break;

        case KBDUS_REQ_STATE_AWAITING_GET_:
        case KBDUS_REQ_STATE_AWAITING_COMPLETION_:

            kbdus_inverter_wrapper_to_free_(
                inverter, wrapper, -ETIMEDOUT, -ETIMEDOUT);

            ret = KBDUS_INVERTER_BLK_EH_DONE_;

            break;

        default:

            WARN_ON(true);

            ret = KBDUS_INVERTER_BLK_EH_DONE_;

            break;
        }
    }

    // unlock request lock

    spin_unlock_irqrestore(&inverter->reqs_lock, flags);

    // return result

    return ret;
}

/* -------------------------------------------------------------------------- */

const struct kbdus_inverter_item *
    kbdus_inverter_begin_item_get(struct kbdus_inverter *inverter)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;

    while (true)
    {
        // wait until a request is awaiting get (or until we are interrupted)

        if (wait_for_completion_interruptible(&inverter->item_is_awaiting_get)
            != 0)
        {
            return ERR_PTR(-ERESTARTSYS);
        }

        // lock request lock

        spin_lock_irq(&inverter->reqs_lock);

        // check if request is a notification request

        if (inverter->flags & KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_)
        {
            inverter->flags &= ~KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_;

            spin_unlock_irq(&inverter->reqs_lock);
            return &kbdus_inverter_req_flush_terminate_;
        }

        if (inverter->flags
            & (KBDUS_INVERTER_FLAG_DEACTIVATED_
               | KBDUS_INVERTER_FLAG_TERMINATED_))
        {
            spin_unlock_irq(&inverter->reqs_lock);
            return &kbdus_inverter_req_terminate_;
        }

        if (inverter->flags & KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_)
        {
            inverter->flags &= ~KBDUS_INVERTER_FLAG_SEND_DEVICE_AVAILABLE_;

            spin_unlock_irq(&inverter->reqs_lock);
            return &kbdus_inverter_req_dev_available_;
        }

        // ensure request awaiting get exists (might have been canceled in the
        // meantime)

        if (!list_empty(&inverter->reqs_awaiting_get))
            break;

        // unlock request lock

        spin_unlock_irq(&inverter->reqs_lock);
    }

    // get request wrapper

    wrapper = list_first_entry(
        &inverter->reqs_awaiting_get, struct kbdus_inverter_req_wrapper_, list);

    // advance request state

    kbdus_inverter_wrapper_to_being_gotten_(inverter, wrapper);

    // unlock request lock

    spin_unlock_irq(&inverter->reqs_lock);

    // return request

    return &wrapper->item;
}

void kbdus_inverter_commit_item_get(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;

    switch (item->type)
    {
    case KBDUS_ITEM_TYPE_DEVICE_AVAILABLE:
    case KBDUS_ITEM_TYPE_TERMINATE:
    case KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE:
        // nothing to be done
        break;

    default:

        // get request wrapper

        wrapper = container_of(item, struct kbdus_inverter_req_wrapper_, item);

        // lock request lock

        spin_lock_irq(&inverter->reqs_lock);

        // perform some sanity checks

#if KBDUS_DEBUG
        WARN_ON(wrapper->state != KBDUS_REQ_STATE_BEING_GOTTEN_);
#endif

        // advance request state

        if (inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_)
        {
            kbdus_inverter_wrapper_cancel_due_to_termination_(
                inverter, wrapper);
        }
        else
        {
            kbdus_inverter_wrapper_to_awaiting_completion_(inverter, wrapper);
        }

        // unlock request lock

        spin_unlock_irq(&inverter->reqs_lock);

        break;
    }
}

void kbdus_inverter_abort_item_get(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;

    switch (item->type)
    {
    case KBDUS_ITEM_TYPE_DEVICE_AVAILABLE:

        // resubmit "device available" request

        kbdus_inverter_submit_device_available_notification(inverter);

        break;

    case KBDUS_ITEM_TYPE_TERMINATE:
        // nothing to be done
        break;

    case KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE:
        inverter->flags |= KBDUS_INVERTER_FLAG_DEACTIVATED_NOT_FLUSHED_;
        break;

    default:

        // get request wrapper

        wrapper = container_of(item, struct kbdus_inverter_req_wrapper_, item);

        // lock request lock

        spin_lock_irq(&inverter->reqs_lock);

        // perform some sanity checks

#if KBDUS_DEBUG
        WARN_ON(wrapper->state != KBDUS_REQ_STATE_BEING_GOTTEN_);
#endif

        // advance request state

        if (inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_)
        {
            kbdus_inverter_wrapper_cancel_due_to_termination_(
                inverter, wrapper);
        }
        else
        {
            kbdus_inverter_wrapper_to_awaiting_get_(inverter, wrapper);
        }

        // unlock request lock

        spin_unlock_irq(&inverter->reqs_lock);

        break;
    }
}

const struct kbdus_inverter_item *kbdus_inverter_begin_item_completion(
    struct kbdus_inverter *inverter, u16 request_handle_index,
    u64 request_handle_seqnum)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;

    // get wrapper and ensure that handle index is valid

    wrapper =
        kbdus_inverter_handle_index_to_wrapper_(inverter, request_handle_index);

    if (!wrapper)
        return ERR_PTR(-EINVAL);

    // lock request lock

    spin_lock_irq(&inverter->reqs_lock);

    // ensure that handle seqnum matches

    if (wrapper->item.handle_seqnum != request_handle_seqnum)
    {
        spin_unlock_irq(&inverter->reqs_lock);
        return NULL;
    }

    // ensure that request is awaiting completion

    if (wrapper->state != KBDUS_REQ_STATE_AWAITING_COMPLETION_)
    {
        spin_unlock_irq(&inverter->reqs_lock);
        return ERR_PTR(-EINVAL);
    }

    // advance request state

    kbdus_inverter_wrapper_to_being_completed_(inverter, wrapper);

    // unlock request lock

    spin_unlock_irq(&inverter->reqs_lock);

    // return request

    return &wrapper->item;
}

void kbdus_inverter_commit_item_completion(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item,
    int neg_errno)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;
    int neg_errno_ioctl;

    // get request wrapper

    wrapper = container_of(item, struct kbdus_inverter_req_wrapper_, item);

    // lock request lock

    spin_lock_irq(&inverter->reqs_lock);

    // perform some sanity checks

#if KBDUS_DEBUG
    WARN_ON(wrapper->state != KBDUS_REQ_STATE_BEING_COMPLETED_);
    WARN_ON(
        wrapper->item.type == KBDUS_ITEM_TYPE_DEVICE_AVAILABLE
        || wrapper->item.type == KBDUS_ITEM_TYPE_TERMINATE
        || wrapper->item.type == KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE);
#endif

    // advance item state

    if (inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_)
    {
        kbdus_inverter_wrapper_cancel_due_to_termination_(inverter, wrapper);
    }
    else
    {
        neg_errno_ioctl = neg_errno;

        if (unlikely(
                neg_errno != 0 && neg_errno != -ENOLINK && neg_errno != -ENOSPC
                && neg_errno != -ETIMEDOUT))
        {
            neg_errno = -EIO;
        }

        if (unlikely(
                neg_errno_ioctl < -133 || neg_errno_ioctl > 0
                || neg_errno_ioctl == -ENOSYS))
        {
            neg_errno_ioctl = -EIO;
        }

        kbdus_inverter_wrapper_to_free_(
            inverter, wrapper, neg_errno, neg_errno_ioctl);
    }

    // unlock request lock

    spin_unlock_irq(&inverter->reqs_lock);
}

void kbdus_inverter_abort_item_completion(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item)
{
    struct kbdus_inverter_req_wrapper_ *wrapper;

    // get request wrapper

    wrapper = container_of(item, struct kbdus_inverter_req_wrapper_, item);

    // lock request lock

    spin_lock_irq(&inverter->reqs_lock);

    // perform some sanity checks

#if KBDUS_DEBUG
    WARN_ON(wrapper->state != KBDUS_REQ_STATE_BEING_COMPLETED_);
    WARN_ON(
        wrapper->item.type == KBDUS_ITEM_TYPE_DEVICE_AVAILABLE
        || wrapper->item.type == KBDUS_ITEM_TYPE_TERMINATE
        || wrapper->item.type == KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE);
#endif

    // advance request state

    if (inverter->flags & KBDUS_INVERTER_FLAG_TERMINATED_)
        kbdus_inverter_wrapper_cancel_due_to_termination_(inverter, wrapper);
    else
        kbdus_inverter_wrapper_to_awaiting_completion_(inverter, wrapper);

    // unlock request lock

    spin_unlock_irq(&inverter->reqs_lock);
}

/* -------------------------------------------------------------------------- */
