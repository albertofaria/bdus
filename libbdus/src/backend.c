/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <bdus.h>
#include <libbdus/backend.h>
#include <libbdus/utilities.h>

#include <errno.h>
#include <inttypes.h>
#include <kbdus.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */

static int bdus_backend_on_device_available_default_(struct bdus_ctx *ctx)
{
    if (puts(ctx->path) == EOF)
        return EIO;

    if (fflush(stdout) == EOF)
        return EIO;

    return 0;
}

static bool
    bdus_backend_on_device_available_(struct bdus_ctx *ctx, int thread_index)
{
    // invoke on_device_available() callback

    int ret;

    if (ctx->ops->on_device_available)
    {
        if (ctx->attrs->log)
            bdus_log_thread_no_args_(thread_index, "on_device_available(ctx)");

        ret = ctx->ops->on_device_available(ctx);
    }
    else
    {
        if (ctx->attrs->log)
        {
            bdus_log_thread_no_args_(
                thread_index,
                "on_device_available(ctx) [not implemented, using default"
                " implementation]");
        }

        ret = bdus_backend_on_device_available_default_(ctx);
    }

    if (ret != 0)
    {
        bdus_set_error_append_errno_(
            ret, "Driver callback on_device_available() failed");
        return false;
    }

    // daemonize the current process

    if (!ctx->attrs->dont_daemonize)
    {
        if (ctx->attrs->log)
            bdus_log_no_args_("daemonizing...");

        if (!bdus_daemonize_())
        {
            bdus_set_error_(EINVAL, "Failed to daemonize the current process");
            return false;
        }
    }

    // success

    return true;
}

static void bdus_backend_process_flush_request_(
    struct bdus_ctx *ctx, int thread_index, int32_t *out_error)
{
    // log callback invocation

    if (ctx->attrs->log)
        bdus_log_thread_no_args_(thread_index, "flush(ctx)");

    // invoke 'flush' callback

    *out_error = (int32_t)ctx->ops->flush(ctx);
}

static ssize_t bdus_backend_process_request_(
    struct bdus_ctx *ctx, int thread_index, void *payload, uint32_t type,
    uint64_t arg64, uint32_t arg32, int32_t *out_error)
{
    switch (type)
    {
    case KBDUS_ITEM_TYPE_READ:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "read(%p, %" PRIu64 ", %" PRIu32 ", ctx)",
                payload, arg64, arg32);
        }

        // invoke 'read' callback

        *out_error = (int32_t)ctx->ops->read(payload, arg64, arg32, ctx);

        return *out_error == 0 ? (ssize_t)arg32 : 0;

    case KBDUS_ITEM_TYPE_WRITE:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "write(%p, %" PRIu64 ", %" PRIu32 ", ctx)",
                payload, arg64, arg32);
        }

        // invoke 'write' callback

        *out_error = (int32_t)ctx->ops->write(payload, arg64, arg32, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_WRITE_SAME:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "write_same(%p, %" PRIu64 ", %" PRIu32 ", ctx)",
                payload, arg64, arg32);
        }

        // invoke 'write_same' callback

        *out_error = (int32_t)ctx->ops->write_same(payload, arg64, arg32, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index,
                "write_zeros(%" PRIu64 ", %" PRIu32 ", false, ctx)", arg64,
                arg32);
        }

        // invoke 'write_zeros' callback

        *out_error = (int32_t)ctx->ops->write_zeros(arg64, arg32, false, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index,
                "write_zeros(%" PRIu64 ", %" PRIu32 ", true, ctx)", arg64,
                arg32);
        }

        // invoke 'write_zeros' callback

        *out_error = (int32_t)ctx->ops->write_zeros(arg64, arg32, true, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_FUA_WRITE:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "fua_write(%p, %" PRIu64 ", %" PRIu32 ", ctx)",
                payload, arg64, arg32);
        }

        // invoke 'fua_write' callback

        *out_error = (int32_t)ctx->ops->fua_write(payload, arg64, arg32, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_FLUSH:

        bdus_backend_process_flush_request_(ctx, thread_index, out_error);

        return 0;

    case KBDUS_ITEM_TYPE_DISCARD:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "discard(%" PRIu64 ", %" PRIu32 ", ctx)", arg64,
                arg32);
        }

        // invoke 'discard' callback

        *out_error = (int32_t)ctx->ops->discard(arg64, arg32, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_SECURE_ERASE:

        // log callback invocation

        if (ctx->attrs->log)
        {
            bdus_log_thread_(
                thread_index, "secure_erase(%" PRIu64 ", %" PRIu32 ", ctx)",
                arg64, arg32);
        }

        // invoke 'secure_erase' callback

        *out_error = (int32_t)ctx->ops->secure_erase(arg64, arg32, ctx);

        return 0;

    case KBDUS_ITEM_TYPE_IOCTL:

        // log callback invocation

        if (ctx->attrs->log)
        {
            if (_IOC_DIR(arg32) == _IOC_NONE)
            {
                bdus_log_thread_(
                    thread_index,
                    "ioctl(_IO(0x%" PRIX32 ", %" PRIu32 "), NULL, ctx)",
                    (uint32_t)_IOC_TYPE(arg32), (uint32_t)_IOC_NR(arg32));
            }
            else
            {
                const char *cmd_macro;

                // clang-format off
                switch (_IOC_DIR(arg32))
                {
                case _IOC_READ:              cmd_macro = "_IOR";  break;
                case _IOC_WRITE:             cmd_macro = "_IOW";  break;
                case _IOC_READ | _IOC_WRITE: cmd_macro = "_IOWR"; break;
                default:                     cmd_macro = NULL;    break;
                }
                // clang-format on

                bdus_log_thread_(
                    thread_index,
                    "ioctl(%s(0x%" PRIX32 ", %" PRIu32 ", %" PRIu32
                    "), %p, ctx)",
                    cmd_macro, (uint32_t)_IOC_TYPE(arg32),
                    (uint32_t)_IOC_NR(arg32), (uint32_t)_IOC_SIZE(arg32),
                    payload);
            }
        }

        if (_IOC_DIR(arg32) == _IOC_NONE)
        {
            // invoke 'ioctl' callback

            *out_error = (int32_t)ctx->ops->ioctl(arg32, NULL, ctx);

            return 0;
        }
        else
        {
            // clear payload buffer if write-only ioctl

            if (_IOC_DIR(arg32) == _IOC_WRITE)
                memset(payload, 0, (size_t)_IOC_SIZE(arg32));

            // invoke 'ioctl' callback

            *out_error = (int32_t)ctx->ops->ioctl(arg32, payload, ctx);

            return *out_error == 0 && (_IOC_DIR(arg32) & _IOC_WRITE)
                ? (ssize_t)_IOC_SIZE(arg32)
                : 0;
        }

    default:

        // unknown request type

        return (ssize_t)(-1);
    }
}

/* -------------------------------------------------------------------------- */

enum
{
    BDUS_STATUS_DEVICE_AVAILABLE_,
    BDUS_STATUS_TERMINATE_,
    BDUS_STATUS_ERROR_,
};

struct bdus_thread_ctx_
{
    struct bdus_ctx *ctx;
    int control_fd;

    pthread_t thread;
    size_t thread_index;
    union kbdus_reply_or_item *rai;
    void *payload;
    bool allow_device_available;

    int status;
    int error_errno;
    const char *error_message;
};

static size_t bdus_max_request_payload_size_(const struct bdus_ctx *ctx)
{
    size_t size = (size_t)ctx->attrs->max_read_write_size;

    if (ctx->ops->write_same)
        size = bdus_max_(size, (size_t)ctx->attrs->logical_block_size);

    if (ctx->ops->ioctl)
        size = bdus_max_(size, (size_t)1 << 14);

    return size;
}

/* -------------------------------------------------------------------------- */

static bool bdus_send_reply_and_receive_item_(struct bdus_thread_ctx_ *context)
{
    const int ret = bdus_ioctl_arg_retry_(
        context->control_fd, KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM,
        (void *)context->thread_index);

    if (ret == 0)
    {
        return true;
    }
    else
    {
        context->status      = BDUS_STATUS_ERROR_;
        context->error_errno = errno;
        context->error_message =
            "Failed to issue ioctl with command"
            " KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM to /dev/bdus-control";

        return false;
    }
}

static bool bdus_process_item_(struct bdus_thread_ctx_ *context)
{
    ssize_t reply_payload_size;

    switch (context->rai->item.type)
    {
    case KBDUS_ITEM_TYPE_DEVICE_AVAILABLE:

        if (context->allow_device_available)
        {
            context->status = BDUS_STATUS_DEVICE_AVAILABLE_;
        }
        else
        {
            context->status      = BDUS_STATUS_ERROR_;
            context->error_errno = EINVAL;
            context->error_message =
                "Received \"device available\" notification more than once";
        }

        return false;

    case KBDUS_ITEM_TYPE_TERMINATE:

        context->status = BDUS_STATUS_TERMINATE_;

        return false;

    case KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE:

        bdus_backend_process_flush_request_(
            context->ctx, (int)context->thread_index,
            &context->rai->reply.error);

        if (context->rai->reply.error == 0)
        {
            context->status = BDUS_STATUS_TERMINATE_;
        }
        else
        {
            context->status        = BDUS_STATUS_ERROR_;
            context->error_errno   = EIO;
            context->error_message = "Failed to flush before terminating";
        }

        return false;

    default:

        reply_payload_size = bdus_backend_process_request_(
            context->ctx, (int)context->thread_index, context->payload,
            context->rai->item.type, context->rai->item.arg64,
            context->rai->item.arg32, &context->rai->reply.error);

        if (reply_payload_size < 0)
        {
            context->status        = BDUS_STATUS_ERROR_;
            context->error_errno   = EINVAL;
            context->error_message = "Received item of unknown type";

            return false;
        }
        else if (context->rai->reply.error == (int32_t)bdus_abort)
        {
            context->status        = BDUS_STATUS_ERROR_;
            context->error_errno   = EIO;
            context->error_message = "Driver aborted";

            return false;
        }

        return true;
    }
}

static void bdus_work_loop_(struct bdus_thread_ctx_ *context)
{
    context->rai->common.user_ptr_or_buffer_index =
        (uint32_t)context->thread_index;

    context->rai->common.handle_index            = UINT16_C(0);
    context->rai->common.use_preallocated_buffer = UINT8_C(1);

    // receive items, process them, and send replies

    while (true)
    {
        if (!bdus_send_reply_and_receive_item_(context))
            break; // error

        if (!bdus_process_item_(context))
            break; // error or received notification
    }

    // check if error occurred

    if (context->status == BDUS_STATUS_ERROR_)
    {
        // work loop failed, terminate control file description so that other
        // worker threads terminate

        if (bdus_ioctl_retry_(context->control_fd, KBDUS_IOCTL_TERMINATE) != 0)
            abort();
    }
}

static void *bdus_work_loop_void_(void *context)
{
    bdus_work_loop_(context);
    return NULL;
}

/* -------------------------------------------------------------------------- */

static bool bdus_run_3_(struct bdus_thread_ctx_ *contexts, size_t num_threads)
{
    // run multi-threaded until device terminates or error

    for (size_t i = 1; i < num_threads; ++i)
    {
        const int ret = pthread_create(
            &contexts[i].thread, NULL, bdus_work_loop_void_, &contexts[i]);

        if (ret != 0)
        {
            if (bdus_ioctl_retry_(contexts[i].control_fd, KBDUS_IOCTL_TERMINATE)
                != 0)
            {
                abort();
            }

            for (; i > 1; --i)
            {
                if (pthread_join(contexts[i - 1].thread, NULL) != 0)
                    abort();
            }

            bdus_set_error_append_errno_(ret, "pthread_create() failed");
            return false;
        }
    }

    // run first worker thread

    bdus_work_loop_(&contexts[0]);

    // join worker threads

    for (size_t i = 1; i < num_threads; ++i)
    {
        if (pthread_join(contexts[i].thread, NULL) != 0)
            abort();
    }

    // check if any thread failed

    for (size_t i = 0; i < num_threads; ++i)
    {
        if (contexts[i].status == BDUS_STATUS_ERROR_)
        {
            bdus_set_error_append_errno_(
                contexts[i].error_errno, "%s", contexts[i].error_message);

            return false;
        }
    }

    // success

    return true;
}

static bool bdus_run_2_(
    struct bdus_ctx *ctx, struct bdus_thread_ctx_ *contexts, size_t num_threads)
{
    // run single-threaded until device becomes available, terminates, or error

    {
        struct bdus_thread_ctx_ *const context = &contexts[0];

        context->allow_device_available = true;

        bdus_work_loop_(&contexts[0]);

        switch (context->status)
        {
        case BDUS_STATUS_DEVICE_AVAILABLE_:
            break;

        case BDUS_STATUS_TERMINATE_:
            return true;

        case BDUS_STATUS_ERROR_:
            bdus_set_error_append_errno_(
                context->error_errno, "%s", context->error_message);
            return false;
        }

        context->allow_device_available = false;

        // invoke on_device_available() callback and daemonize the current
        // process

        if (!bdus_backend_on_device_available_(ctx, (int)context->thread_index))
            return false;
    }

    // multi-threaded

    return bdus_run_3_(contexts, num_threads);
}

static void *bdus_mmap_(int control_fd, size_t offset, size_t length)
{
    void *ptr = mmap(
        NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
        control_fd, (off_t)offset);

    if (ptr == MAP_FAILED)
    {
        bdus_set_error_append_errno_(
            errno,
            "mmap() on /dev/bdus-control at offset %zu of length %zu failed",
            offset, length);

        ptr = NULL;
    }

    return ptr;
}

bool bdus_backend_run_(
    int control_fd, struct bdus_ctx *ctx, uint32_t max_outstanding_reqs)
{
    const size_t num_threads = (size_t)ctx->attrs->max_concurrent_callbacks;

    const size_t page_size = bdus_get_page_size_();

    if (page_size == 0)
        return false;

    // allocate and initialize context structures

    struct bdus_thread_ctx_ *const contexts =
        calloc(num_threads, sizeof(*contexts));

    if (!contexts)
    {
        bdus_set_error_append_errno_(errno, "calloc() failed");
        return false;
    }

    // map memory

    const size_t max_payload_size = bdus_max_request_payload_size_(ctx);

    const size_t rai_memory_size =
        bdus_round_up_(max_outstanding_reqs * 64, page_size);

    const size_t single_payload_memory_size =
        bdus_round_up_(max_payload_size, page_size);

    void *const rai_memory = bdus_mmap_(control_fd, 0, rai_memory_size);

    if (!rai_memory)
    {
        free(contexts);
        return false;
    }

    for (size_t i = 0; i < num_threads; ++i)
    {
        struct bdus_thread_ctx_ *const c = &contexts[i];

        c->ctx        = ctx;
        c->control_fd = control_fd;

        c->thread_index           = i;
        c->allow_device_available = false;

        c->rai = (union kbdus_reply_or_item *)((char *)rai_memory + 64 * i);

        if (max_payload_size == 0)
        {
            c->payload = NULL;
        }
        else
        {
            c->payload = bdus_mmap_(
                control_fd, rai_memory_size + single_payload_memory_size * i,
                single_payload_memory_size);

            if (!c->payload)
            {
                if (max_payload_size > 0)
                {
                    for (; i > 0; --i)
                    {
                        if (munmap(
                                contexts[i - 1].payload,
                                single_payload_memory_size)
                            != 0)
                        {
                            abort();
                        }
                    }
                }

                if (munmap(rai_memory, rai_memory_size) != 0)
                    abort();

                free(contexts);

                return false;
            }
        }
    }

    // delegate things

    const bool success = bdus_run_2_(ctx, contexts, num_threads);

    // free context structures and unmap memory

    if (max_payload_size > 0)
    {
        for (size_t i = num_threads; i > 0; --i)
        {
            if (munmap(contexts[i - 1].payload, single_payload_memory_size)
                != 0)
            {
                abort();
            }
        }
    }

    if (munmap(rai_memory, rai_memory_size) != 0)
        abort();

    free(contexts);

    // return success indication

    return success;
}

/* -------------------------------------------------------------------------- */
