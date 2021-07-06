/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500

#include <bdus.h>
#include <libbdus/backend.h>
#include <libbdus/utilities.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <kbdus.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */

static void bdus_set_error_control_ioctl_generic_(
    int errno_value, const char *ioctl_command_name)
{
    bdus_set_error_append_errno_(
        errno_value, "ioctl() on /dev/bdus-control with command %s failed",
        ioctl_command_name);
}

static bool bdus_check_privileges_(void)
{
    // This check is not required for correctness, but is included to give a
    // better error message if a user tries to run a driver or manage a device
    // without having sufficient privileges.
    //
    // If we didn't check this and kbdus was unloaded, we would silently fail to
    // load it and the subsequent attempt to open /dev/bdus-control would report
    // that it doesn't exist, instead of reporting that the caller doesn't have
    // enough permissions.
    //
    // However, the check for root is not entirely correct, as what a user
    // really needs to be able to perform these tasks is the CAP_SYS_ADMIN
    // capability (and also the ability to open /dev/bdus-control).
    //
    // It would be preferable to check the process capabilities, but that
    // introduces a dependency on libcap, which is not available out-of-the-box
    // in several Linux distribution.

    if (geteuid() == (uid_t)0)
    {
        return true;
    }
    else
    {
        bdus_set_error_(
            EPERM, "Insufficient privileges, must be run as the root user");

        return false;
    }
}

static void bdus_try_to_load_kbdus_(void)
{
    // The ideal would be to use libkmod to load kbdus, but it is not available
    // out-of-the-box in several Linux distributions.
    //
    // Another option would be to add kbdus to the list of modules to be loaded
    // at boot, but the location and format of this list vary across Linux
    // distributions.
    //
    // We thus simply exec() /sbin/modprobe, which should work everywhere (/sbin
    // is a standardized Linux directory, and modprobe is surely always
    // available), but nevertheless fail quietly if something goes wrong to
    // allow for unforeseen setups where kbdus can't be loaded in this way for
    // some reason.

    const int previous_errno = errno;

    const pid_t fork_result = fork();

    if (fork_result < 0)
    {
        // error
    }
    else if (fork_result > 0)
    {
        // in parent

        waitpid(fork_result, NULL, 0);
    }
    else
    {
        // in child

        // redirect stdin, stdout, and stderr to /dev/null

        if (!bdus_redirect_to_dev_null_(STDIN_FILENO, O_RDONLY))
            _exit(1);

        if (!bdus_redirect_to_dev_null_(STDOUT_FILENO, O_WRONLY))
            _exit(1);

        if (!bdus_redirect_to_dev_null_(STDERR_FILENO, O_WRONLY))
            _exit(1);

        // exec modprobe

        const char modprobe_path[]       = "/sbin/modprobe";
        const char *const modprobe_env[] = { NULL };

        execle(
            modprobe_path, modprobe_path, "kbdus", (char *)NULL, modprobe_env);

        // error

        _exit(1);
    }

    errno = previous_errno;
}

static bool bdus_check_version_compatibility_(int control_fd)
{
    // get kbdus version

    struct kbdus_version kbdus_ver;

    if (bdus_ioctl_arg_retry_(control_fd, KBDUS_IOCTL_GET_VERSION, &kbdus_ver)
        == -1)
    {
        bdus_set_error_control_ioctl_generic_(errno, "KBDUS_IOCTL_GET_VERSION");
        return false;
    }

    // check if kbdus version is compatible

    const struct bdus_version *const libbdus_ver = bdus_get_libbdus_version();

    if (kbdus_ver.major == libbdus_ver->major
        && kbdus_ver.minor == libbdus_ver->minor
        && kbdus_ver.patch >= libbdus_ver->patch)
    {
        return true;
    }
    else
    {
        bdus_set_error_(
            EINVAL,
            "Using libbdus version %" PRIu32 ".%" PRIu32 ".%" PRIu32
            " but kbdus has incompatible version %" PRIu32 ".%" PRIu32
            ".%" PRIu32,
            libbdus_ver->major, libbdus_ver->minor, libbdus_ver->patch,
            kbdus_ver.major, kbdus_ver.minor, kbdus_ver.patch);

        return false;
    }
}

static int bdus_open_control_(bool check_version_compatibility)
{
    // check if driver has sufficient privileges

    if (!bdus_check_privileges_())
        return -1;

    // try to load kbdus

    bdus_try_to_load_kbdus_();

    // open control device

    const int control_fd = bdus_open_retry_("/dev/bdus-control", O_RDWR);

    if (control_fd < 0)
    {
        if (errno == ENOENT)
        {
            bdus_set_error_(
                errno,
                "Failed to load the kbdus module and /dev/bdus-control does not"
                " exist; is kbdus installed?");
        }
        else if (errno == EACCES || errno == EPERM)
        {
            bdus_set_error_(
                errno,
                "Access to /dev/bdus-control was denied; is the program being"
                " run with sufficient privileges?");
        }
        else
        {
            bdus_set_error_append_errno_(
                errno, "Failed to open /dev/bdus-control");
        }

        return -1;
    }

    // check version compatibility with kbdus

    if (check_version_compatibility)
    {
        if (!bdus_check_version_compatibility_(control_fd))
        {
            bdus_close_keep_errno_(control_fd);
            return -1;
        }
    }

    // success

    return control_fd;
}

/* -------------------------------------------------------------------------- */
/* driver development -- common */

#define bdus_log_op_(ops, op)                                                  \
    do                                                                         \
    {                                                                          \
        bdus_log_("   %-21s   %s", #op "()", (ops)->op ? "non-NULL" : "NULL"); \
    } while (0)

#define bdus_log_attr_(attrs, original_attrs, attr, fmt)                       \
    do                                                                         \
    {                                                                          \
        if ((attrs)->attr == (original_attrs)->attr)                           \
        {                                                                      \
            bdus_log_("   %-26s   %" fmt, #attr, (attrs)->attr);               \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            bdus_log_(                                                         \
                "   %-26s   %" fmt " (was %" fmt ")", #attr, (attrs)->attr,    \
                (original_attrs)->attr);                                       \
        }                                                                      \
    } while (0)

#define bdus_log_attr_bool_(attrs, original_attrs, attr)                       \
    do                                                                         \
    {                                                                          \
        if ((attrs)->attr == (original_attrs)->attr)                           \
        {                                                                      \
            bdus_log_(                                                         \
                "   %-26s   %s", #attr, (attrs)->attr ? "true" : "false");     \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            bdus_log_(                                                         \
                "   %-26s   %s (was %s)", #attr,                               \
                (attrs)->attr ? "true" : "false",                              \
                (original_attrs)->attr ? "true" : "false");                    \
        }                                                                      \
    } while (0)

static void bdus_log_ops_and_attrs_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    const struct bdus_attrs *original_attrs)
{
    bdus_log_no_args_("struct bdus_ops:");
    bdus_log_op_(ops, initialize);
    bdus_log_op_(ops, on_device_available);
    bdus_log_op_(ops, terminate);
    bdus_log_op_(ops, read);
    bdus_log_op_(ops, write);
    bdus_log_op_(ops, write_same);
    bdus_log_op_(ops, write_zeros);
    bdus_log_op_(ops, fua_write);
    bdus_log_op_(ops, flush);
    bdus_log_op_(ops, discard);
    bdus_log_op_(ops, secure_erase);
    bdus_log_op_(ops, ioctl);

    bdus_log_no_args_("struct bdus_attrs:");
    bdus_log_attr_(attrs, original_attrs, logical_block_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, physical_block_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, size, PRIu64);
    bdus_log_attr_(attrs, original_attrs, max_read_write_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, max_write_same_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, max_write_zeros_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, max_discard_erase_size, PRIu32);
    bdus_log_attr_(attrs, original_attrs, max_concurrent_callbacks, PRIu32);
    bdus_log_attr_bool_(attrs, original_attrs, disable_partition_scanning);
    bdus_log_attr_bool_(attrs, original_attrs, recoverable);
    bdus_log_attr_bool_(attrs, original_attrs, dont_daemonize);
    bdus_log_attr_bool_(attrs, original_attrs, log);
}

static bool bdus_execute_driver_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    const struct bdus_attrs *original_attrs, void *private_data, int control_fd,
    const struct kbdus_device_config *device_config,
    uint32_t max_outstanding_reqs, bool is_rerun)
{
    // get device path

    char dev_path[32];

    const int ret = snprintf(
        dev_path, sizeof(dev_path), "/dev/bdus-%" PRIu64, device_config->id);

    if (ret <= 0 || (size_t)ret >= sizeof(dev_path))
    {
        bdus_set_error_(EIO, "snprintf() failed");
        return false;
    }

    // create bdus_ctx structure

    struct bdus_ctx ctx = {
        .id           = device_config->id,
        .path         = dev_path,
        .ops          = ops,
        .attrs        = attrs,
        .is_rerun     = is_rerun,
        .private_data = private_data,
        .major        = device_config->major,
        .minor        = device_config->minor,
    };

    // log attribute adjustment

    if (ctx.attrs->log)
        bdus_log_ops_and_attrs_(ops, attrs, original_attrs);

    // invoke `initialize()` callback

    if (ctx.ops->initialize)
    {
        if (ctx.attrs->log)
            bdus_log_thread_no_args_(0, "initialize(ctx)");

        const int ret = ctx.ops->initialize(&ctx);

        if (ret != 0)
        {
            bdus_set_error_append_errno_(
                ret, "Driver callback initialize() failed");

            return false;
        }
    }
    else
    {
        if (ctx.attrs->log)
            bdus_log_thread_no_args_(0, "initialize(ctx) [not implemented]");
    }

    // delegate work to backend

    const bool success =
        bdus_backend_run_(control_fd, &ctx, max_outstanding_reqs);

    // invoke `terminate()` callback

    if (ctx.ops->terminate)
    {
        if (ctx.attrs->log)
            bdus_log_thread_no_args_(0, "terminate(ctx)");

        const int e   = errno;
        const int ret = ctx.ops->terminate(&ctx);
        errno         = e;

        if (success && ret != 0) // avoid replacing previous error
        {
            bdus_set_error_append_errno_(
                ret, "Driver callback terminate() failed");

            return false;
        }
    }
    else
    {
        if (ctx.attrs->log)
            bdus_log_thread_no_args_(0, "terminate(ctx) [not implemented]");
    }

    // report success to kbdus

    if (success)
    {
        if (bdus_ioctl_retry_(control_fd, KBDUS_IOCTL_MARK_AS_SUCCESSFUL) != 0)
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_MARK_AS_SUCCESSFUL");

            return false;
        }
    }

    // return

    return success;
}

/* -------------------------------------------------------------------------- */
/* driver development -- bdus_run() */

static bool bdus_validate_ops_run_(const struct bdus_ops *ops)
{
    // 'fua_write' implies 'flush'

    if (ops->fua_write && !ops->flush)
    {
        bdus_set_error_(
            EINVAL,
            "The driver implements callback 'fua_write' but not 'flush'");

        return false;
    }

    // success

    return true;
}

static bool bdus_validate_attrs_run_(const struct bdus_attrs *attrs)
{
    // get system page size

    const size_t page_size = bdus_get_page_size_();

    if (page_size == 0)
        return false;

    // validate 'logical_block_size'

    if (!bdus_is_power_of_two_(attrs->logical_block_size)
        || attrs->logical_block_size < UINT32_C(512)
        || attrs->logical_block_size > (uint32_t)page_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'logical_block_size', must be a power of two "
            "greater than or equal to 512 and less than or equal to the "
            "system's page size (which is %zu)",
            attrs->logical_block_size, page_size);

        return false;
    }

    // validate 'physical_block_size'

    if (attrs->physical_block_size != 0
        && (!bdus_is_power_of_two_(attrs->physical_block_size)
            || attrs->physical_block_size < attrs->logical_block_size
            || attrs->physical_block_size > (uint32_t)page_size))
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'physical_block_size', must be 0 or a power of two "
            "greater than or equal to attribute 'logical_block_size' (which is "
            "%" PRIu32
            ") and less than or equal to the system's page size (which is %zu)",
            attrs->physical_block_size, attrs->logical_block_size, page_size);

        return false;
    }

    // validate 'size'

    const uint32_t adjusted_physical_block_size =
        bdus_max_(attrs->physical_block_size, attrs->logical_block_size);

    if (!bdus_is_positive_multiple_of_(
            attrs->size, (uint64_t)adjusted_physical_block_size))
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu64
            " for attribute 'size', must be a positive multiple of attribute "
            "'physical_block_size' (which is %" PRIu32 ")",
            attrs->size, adjusted_physical_block_size);

        return false;
    }

    // validate 'max_read_write_size'

    if (attrs->max_read_write_size != 0
        && attrs->max_read_write_size < (uint32_t)page_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_read_write_size', must be 0 or greater than "
            "or equal to the system's page size (which is %zu)",
            attrs->max_read_write_size, page_size);

        return false;
    }

    // validate 'max_write_same_size'

    if (attrs->max_write_same_size != 0
        && attrs->max_write_same_size < attrs->logical_block_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_write_same_size', must be 0 or greater than "
            "or equal to attribute 'logical_block_size' (which is %" PRIu32 ")",
            attrs->max_write_same_size, attrs->logical_block_size);

        return false;
    }

    // validate 'max_write_zeros_size'

    if (attrs->max_write_zeros_size != 0
        && attrs->max_write_zeros_size < attrs->logical_block_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_write_zeros_size', must be 0 or greater than "
            "or equal to attribute 'logical_block_size' (which is %" PRIu32 ")",
            attrs->max_write_zeros_size, attrs->logical_block_size);

        return false;
    }

    // validate 'max_discard_erase_size'

    if (attrs->max_discard_erase_size != 0
        && attrs->max_discard_erase_size < attrs->logical_block_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_discard_erase_size', must be 0 or greater "
            "than or equal to attribute 'logical_block_size' (which is %" PRIu32
            ")",
            attrs->max_discard_erase_size, attrs->logical_block_size);

        return false;
    }

    // success

    return true;
}

static bool bdus_run_impl_(
    const struct bdus_ops *ops_copy, struct bdus_attrs *attrs_copy,
    void *private_data, int control_fd)
{
    const struct bdus_attrs original_attrs = *attrs_copy;

    // validate operations and attributes

    if (!bdus_validate_ops_run_(ops_copy))
        return false;

    if (!bdus_validate_attrs_run_(attrs_copy))
        return false;

    // create configuration

    if (attrs_copy->max_concurrent_callbacks == 0)
        attrs_copy->max_concurrent_callbacks = 1;

    struct kbdus_device_and_fd_config kbdus_config =
    {
        .device =
        {
            .size = attrs_copy->size,

            .logical_block_size  = attrs_copy->logical_block_size,
            .physical_block_size = attrs_copy->physical_block_size,

            .max_read_write_size    = attrs_copy->max_read_write_size,
            .max_write_same_size    = attrs_copy->max_write_same_size,
            .max_write_zeros_size   = attrs_copy->max_write_zeros_size,
            .max_discard_erase_size = attrs_copy->max_discard_erase_size,

            .max_outstanding_reqs   = 2 * attrs_copy->max_concurrent_callbacks,

            .supports_read          = (ops_copy->read         != NULL),
            .supports_write         = (ops_copy->write        != NULL),
            .supports_write_same    = (ops_copy->write_same   != NULL),
            .supports_write_zeros   = (ops_copy->write_zeros  != NULL),
            .supports_fua_write     = (ops_copy->fua_write    != NULL),
            .supports_flush         = (ops_copy->flush        != NULL),
            .supports_discard       = (ops_copy->discard      != NULL),
            .supports_secure_erase  = (ops_copy->secure_erase != NULL),
            .supports_ioctl         = (ops_copy->ioctl        != NULL),

            .rotational             = attrs_copy->rotational_,
            .merge_requests         = !attrs_copy->dont_merge_requests_,

            .enable_partition_scanning =
                !attrs_copy->disable_partition_scanning,

            .recoverable = attrs_copy->recoverable,
        },

        .fd =
        {
            .num_preallocated_buffers = attrs_copy->max_concurrent_callbacks,
        },
    };

    // create device

    if (bdus_ioctl_arg_retry_(
            control_fd, KBDUS_IOCTL_CREATE_DEVICE, &kbdus_config)
        != 0)
    {
        if (errno == ENOSPC)
        {
            bdus_set_error_(errno, "Too many devices");
        }
        else
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_CREATE_DEVICE");
        }

        return false;
    }

    // adjust attributes

    attrs_copy->physical_block_size = kbdus_config.device.physical_block_size;

    attrs_copy->max_read_write_size  = kbdus_config.device.max_read_write_size;
    attrs_copy->max_write_same_size  = kbdus_config.device.max_write_same_size;
    attrs_copy->max_write_zeros_size = kbdus_config.device.max_write_zeros_size;

    attrs_copy->max_discard_erase_size =
        kbdus_config.device.max_discard_erase_size;

    attrs_copy->max_concurrent_callbacks =
        kbdus_config.fd.num_preallocated_buffers;

    // delegate remaining work

    return bdus_execute_driver_(
        ops_copy, attrs_copy, &original_attrs, private_data, control_fd,
        &kbdus_config.device, kbdus_config.device.max_outstanding_reqs, false);
}

BDUS_EXPORT_ bool bdus_run_0_1_0_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
    // copy operations and attributes

    const struct bdus_ops ops_copy = *ops;
    struct bdus_attrs attrs_copy   = *attrs;

    // open control device

    const int control_fd = bdus_open_control_(true);

    if (control_fd < 0)
        return false;

    // delegate remaining work

    const bool success =
        bdus_run_impl_(&ops_copy, &attrs_copy, private_data, control_fd);

    // close control device

    bdus_close_keep_errno_(control_fd);

    // return success indication

    return success;
}

BDUS_EXPORT_ bool bdus_run_0_1_1_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
    return bdus_run_0_1_0_(ops, attrs, private_data);
}

/* -------------------------------------------------------------------------- */
/* driver development -- bdus_rerun() */

static bool bdus_validate_ops_rerun_(
    const struct bdus_ops *ops, const struct kbdus_device_config *device_config)
{
    // 'fua_write' implies 'flush'

    if (ops->fua_write && !ops->flush)
    {
        bdus_set_error_(
            EINVAL,
            "The driver implements callback 'fua_write' but not 'flush'");

        return false;
    }

    // check if already supported operations are still supported

    if (device_config->supports_read && !ops->read)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"read\" requests but the driver does not "
            "implement callback 'read'");
    }

    if (device_config->supports_write && !ops->write)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"write\" requests but the driver does not "
            "implement callback 'write'");
    }

    if (device_config->supports_write_same && !ops->write_same)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"write same\" requests but the driver does "
            "not implement callback 'write_same'");
    }

    if (device_config->supports_write_zeros && !ops->write_zeros)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"write zeros\" requests but the driver does "
            "not implement callback 'write_zeros'");
    }

    if (device_config->supports_fua_write && !ops->fua_write)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"FUA write\" requests but the driver does "
            "not implement callback 'fua_write'");
    }

    if (device_config->supports_flush && !ops->flush)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"flush\" requests but the driver does not "
            "implement callback 'flush'");
    }

    // (additionally, must not implement flush if device doesn't support it)
    if (!device_config->supports_flush && ops->flush)
    {
        bdus_set_error_(
            EINVAL,
            "The device does not support \"flush\" requests but the driver "
            "implements callback 'flush'");
    }

    if (device_config->supports_discard && !ops->discard)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"discard\" requests but the driver does not "
            "implement callback 'discard'");
    }

    if (device_config->supports_secure_erase && !ops->secure_erase)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"secure erase\" requests but the driver does "
            "not implement callback 'secure_erase'");
    }

    if (device_config->supports_ioctl && !ops->ioctl)
    {
        bdus_set_error_(
            EINVAL,
            "The device supports \"ioctl\" requests but the driver does not "
            "implement callback 'ioctl'");
    }

    // success

    return true;
}

static bool bdus_validate_attrs_rerun_(
    const struct bdus_attrs *attrs,
    const struct kbdus_device_config *device_config)
{
    // validate 'logical_block_size'

    if (attrs->logical_block_size != 0
        && attrs->logical_block_size != device_config->logical_block_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'logical_block_size', must be 0 or equal to the "
            "existing device's logical block size (which is %" PRIu32 ")",
            attrs->logical_block_size, device_config->logical_block_size);

        return false;
    }

    // validate 'physical_block_size'

    if (attrs->physical_block_size != 0
        && attrs->physical_block_size != device_config->physical_block_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'physical_block_size', must be 0 or equal to the "
            "existing device's physical block size (which is %" PRIu32 ")",
            attrs->physical_block_size, device_config->physical_block_size);

        return false;
    }

    // validate 'size'

    if (attrs->size != 0 && attrs->size != device_config->size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu64
            " for attribute 'size', must be 0 or equal to the existing "
            "device's size (which is %" PRIu64 ")",
            attrs->size, device_config->size);

        return false;
    }

    // validate 'max_read_write_size'

    if (attrs->max_read_write_size != 0
        && attrs->max_read_write_size < device_config->max_read_write_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_read_write_size', must be 0 or greater than "
            "or equal to the original driver's value for this attribute (which "
            "is %" PRIu32 ")",
            attrs->max_read_write_size, device_config->max_read_write_size);

        return false;
    }

    // validate 'max_write_same_size'

    if (attrs->max_write_same_size != 0
        && attrs->max_write_same_size < device_config->max_write_same_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_write_same_size', must be 0 or greater than "
            "or equal to the original driver's value for this attribute (which "
            "is %" PRIu32 ")",
            attrs->max_write_same_size, device_config->max_write_same_size);

        return false;
    }

    // validate 'max_write_zeros_size'

    if (attrs->max_write_zeros_size != 0
        && attrs->max_write_zeros_size < device_config->max_write_zeros_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_write_zeros_size', must be 0 or greater than "
            "or equal to the original driver's value for this attribute (which "
            "is %" PRIu32 ")",
            attrs->max_write_zeros_size, device_config->max_write_zeros_size);

        return false;
    }

    // validate 'max_discard_erase_size'

    if (attrs->max_discard_erase_size != 0
        && attrs->max_discard_erase_size
            < device_config->max_discard_erase_size)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value %" PRIu32
            " for attribute 'max_discard_erase_size', must be 0 or greater "
            "than or equal to the original driver's value for this attribute "
            "(which is %" PRIu32 ")",
            attrs->max_discard_erase_size,
            device_config->max_discard_erase_size);

        return false;
    }

    // validate 'recoverable'

    if (attrs->recoverable != (bool)device_config->recoverable)
    {
        bdus_set_error_(
            EINVAL,
            "Invalid value \"%s\" for attribute 'recoverable', must be equal "
            "to the original driver's value for this attribute (which is "
            "\"%s\")",
            attrs->recoverable ? "true" : "false",
            device_config->recoverable ? "true" : "false");
    }

    // success

    return true;
}

static bool bdus_rerun_impl_(
    uint64_t dev_id, const struct bdus_ops *ops_copy,
    struct bdus_attrs *attrs_copy, void *private_data, int control_fd)
{
    const struct bdus_attrs original_attrs = *attrs_copy;

    // create configuration

    if (attrs_copy->max_concurrent_callbacks == 0)
        attrs_copy->max_concurrent_callbacks = 1;

    struct kbdus_device_and_fd_config kbdus_config = {
        .device = { .id = dev_id },
        .fd     = { .num_preallocated_buffers =
                    attrs_copy->max_concurrent_callbacks },
    };

    // get existing device's configuration

    if (bdus_ioctl_arg_retry_(
            control_fd, KBDUS_IOCTL_GET_DEVICE_CONFIG, &kbdus_config.device)
        != 0)
    {
        if (errno == ENODEV)
        {
            bdus_set_error_(errno, "The device no longer exists");
        }
        else if (errno == EINVAL)
        {
            bdus_set_error_(errno, "The device does not exist");
        }
        else
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_GET_DEVICE_CONFIG");
        }

        return false;
    }

    // validate operations and attributes

    if (!bdus_validate_ops_rerun_(ops_copy, &kbdus_config.device))
        return false;

    if (!bdus_validate_attrs_rerun_(attrs_copy, &kbdus_config.device))
        return false;

    // attach to device (do not retry ioctl if interrupted to avoid race
    // condition where device is destroyed between retries, or another driver
    // starts attaching to it)

    if (ioctl(control_fd, KBDUS_IOCTL_ATTACH_TO_DEVICE, &kbdus_config) != 0)
    {
        if (errno == EINTR)
        {
            bdus_set_error_(errno, "bdus_rerun() was interrupted by a signal");
        }
        else if (errno == ENODEV)
        {
            bdus_set_error_(errno, "The device no longer exists");
        }
        else if (errno == EBUSY)
        {
            bdus_set_error_(
                errno, "The device is not yet available to clients");
        }
        else if (errno == EINPROGRESS)
        {
            bdus_set_error_(
                errno,
                "Another driver is already taking control of the device");
        }
        else
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_ATTACH_TO_DEVICE");
        }

        return false;
    }

    // adjust attributes

    attrs_copy->logical_block_size  = kbdus_config.device.logical_block_size;
    attrs_copy->physical_block_size = kbdus_config.device.physical_block_size;
    attrs_copy->size                = kbdus_config.device.size;

    attrs_copy->max_read_write_size  = kbdus_config.device.max_read_write_size;
    attrs_copy->max_write_same_size  = kbdus_config.device.max_write_same_size;
    attrs_copy->max_write_zeros_size = kbdus_config.device.max_write_zeros_size;

    attrs_copy->max_discard_erase_size =
        kbdus_config.device.max_discard_erase_size;

    attrs_copy->disable_partition_scanning =
        !kbdus_config.device.enable_partition_scanning;

    attrs_copy->max_concurrent_callbacks =
        kbdus_config.fd.num_preallocated_buffers;

    // delegate remaining work

    return bdus_execute_driver_(
        ops_copy, attrs_copy, &original_attrs, private_data, control_fd,
        &kbdus_config.device, kbdus_config.device.max_outstanding_reqs, true);
}

BDUS_EXPORT_ bool bdus_rerun_0_1_0_(
    uint64_t dev_id, const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
    // copy operations and attributes

    const struct bdus_ops ops_copy = *ops;
    struct bdus_attrs attrs_copy   = *attrs;

    // open control device

    const int control_fd = bdus_open_control_(true);

    if (control_fd < 0)
        return false;

    // delegate remaining work

    const bool success = bdus_rerun_impl_(
        dev_id, &ops_copy, &attrs_copy, private_data, control_fd);

    // close control device

    bdus_close_keep_errno_(control_fd);

    // return success indication

    return success;
}

BDUS_EXPORT_ bool bdus_rerun_0_1_1_(
    uint64_t dev_id, const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
    return bdus_rerun_0_1_0_(dev_id, ops, attrs, private_data);
}

/* -------------------------------------------------------------------------- */
/* device management */

BDUS_EXPORT_ bool
    bdus_get_dev_id_from_path_0_1_0_(uint64_t *out_dev_id, const char *dev_path)
{
    // open control device

    const int control_fd = bdus_open_control_(true);

    if (control_fd < 0)
        return false;

    // translate path to id

    uint64_t path_or_id = (uint64_t)dev_path;

    const int ret = bdus_ioctl_arg_retry_(
        control_fd, KBDUS_IOCTL_DEVICE_PATH_TO_ID, &path_or_id);

    // close control device

    bdus_close_keep_errno_(control_fd);

    // check result

    if (ret == 0)
    {
        *out_dev_id = path_or_id;

        return true;
    }
    else
    {
        switch (errno)
        {
        case EACCES:
            bdus_set_error_(
                errno, "Access denied while traversing %s", dev_path);
            break;

        case ELOOP:
            bdus_set_error_(
                errno,
                "Too many symbolic links encountered while traversing %s",
                dev_path);
            break;

        case ENOENT:
        case ENOTDIR:
            bdus_set_error_(errno, "%s does not exist", dev_path);
            break;

        case ENOTBLK:
            bdus_set_error_(errno, "%s is not a block special file", dev_path);
            break;

        case EINVAL:
            bdus_set_error_(
                errno, "%s does not refer to a device created by BDUS",
                dev_path);
            break;

        case ENODEV:
            bdus_set_error_(
                errno, "The device referred to by %s does not exist", dev_path);
            break;

        case ECHILD:
            bdus_set_error_(
                EINVAL,
                "%s refers to a partition of a device created by BDUS and not"
                " to the device itself",
                dev_path);
            break;

        default:
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_DEVICE_PATH_TO_ID");
            break;
        }

        return false;
    }
}

BDUS_EXPORT_ bool bdus_flush_dev_0_1_0_(uint64_t dev_id)
{
    // open control device

    const int control_fd = bdus_open_control_(true);

    if (control_fd < 0)
        return false;

    // flush device

    const int ret =
        bdus_ioctl_arg_retry_(control_fd, KBDUS_IOCTL_FLUSH_DEVICE, &dev_id);

    if (ret != 0)
    {
        if (errno == ENODEV)
            bdus_set_error_(errno, "The device no longer exists");
        else if (errno == EINVAL)
            bdus_set_error_(errno, "The device does not exist");
        else
            bdus_set_error_append_errno_(errno, "Failed to flush the device");
    }

    // close control device

    bdus_close_keep_errno_(control_fd);

    // return success indicator

    return ret == 0;
}

BDUS_EXPORT_ bool bdus_destroy_dev_0_1_0_(uint64_t dev_id)
{
    // open control device

    const int control_fd = bdus_open_control_(true);

    if (control_fd < 0)
        return false;

    // request device destruction

    int ret = bdus_ioctl_arg_retry_(
        control_fd, KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION, &dev_id);

    if (ret != 0)
    {
        if (errno == ENODEV)
        {
            bdus_set_error_(errno, "The device no longer exists");
        }
        else if (errno == EINVAL)
        {
            bdus_set_error_(errno, "The device does not exist");
        }
        else
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION");
        }
    }

    // wait until device is destroyed

    if (ret == 0)
    {
        ret = bdus_ioctl_arg_retry_(
            control_fd, KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED, &dev_id);

        if (ret != 0)
        {
            bdus_set_error_control_ioctl_generic_(
                errno, "KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED");
        }
    }

    // close control device

    bdus_close_keep_errno_(control_fd);

    // return success indicator

    return ret == 0;
}

/* -------------------------------------------------------------------------- */
/* errors */

BDUS_EXPORT_ const char *bdus_get_error_message_0_1_0_(void)
{
    return bdus_get_error_message_();
}

/* -------------------------------------------------------------------------- */
/* versions */

static const struct bdus_version bdus_libbdus_version_ = {
    .major = (uint32_t)BDUS_HEADER_VERSION_MAJOR,
    .minor = (uint32_t)BDUS_HEADER_VERSION_MINOR,
    .patch = (uint32_t)BDUS_HEADER_VERSION_PATCH
};

BDUS_EXPORT_ const struct bdus_version *bdus_get_libbdus_version_0_1_0_(void)
{
    return &bdus_libbdus_version_;
}

BDUS_EXPORT_ bool
    bdus_get_kbdus_version_0_1_0_(struct bdus_version *out_kbdus_version)
{
    // open control device

    const int control_fd = bdus_open_control_(false);

    if (control_fd < 0)
        return false;

    // get kbdus' version

    struct kbdus_version kbdus_ver;
    bool success;

    if (bdus_ioctl_arg_retry_(control_fd, KBDUS_IOCTL_GET_VERSION, &kbdus_ver)
        == 0)
    {
        out_kbdus_version->major = kbdus_ver.major;
        out_kbdus_version->minor = kbdus_ver.minor;
        out_kbdus_version->patch = kbdus_ver.patch;

        success = true;
    }
    else
    {
        bdus_set_error_control_ioctl_generic_(errno, "KBDUS_IOCTL_GET_VERSION");

        success = false;
    }

    // close control device

    bdus_close_keep_errno_(control_fd);

    // return success indication

    return success;
}

/* -------------------------------------------------------------------------- */
