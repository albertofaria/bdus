/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// The driver for a device that mirrors another block device.

// This driver can both create a new device (by running the driver executable
// with only the path to the underlying device as an argument) or replace an
// existing device's driver (by also specifying a path to that device).

// Compile with:
//     cc loop.c -lbdus -o loop

/* -------------------------------------------------------------------------- */

#define _FILE_OFFSET_BITS 64

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <bdus.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // read requested data from underlying device

    while (size > 0)
    {
        ssize_t res = pread(fd, buffer, (size_t)size, (off_t)offset);

        if (res < 0)
        {
            // read failed, retry if interrupted, fail otherwise

            if (errno != EINTR)
                return errno; // not interrupted, return errno
        }
        else if (res == 0)
        {
            // end-of-file, should never happen, abort driver

            return bdus_abort;
        }
        else
        {
            // successfully read some data

            buffer += res;
            offset += (uint64_t)res;
            size -= (uint32_t)res;
        }
    }

    // success

    return 0;
}

static int device_write(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // write given data to underlying device

    while (size > 0)
    {
        ssize_t res = pwrite(fd, buffer, (size_t)size, (off_t)offset);

        if (res < 0)
        {
            // write failed, retry if interrupted, fail otherwise

            if (errno != EINTR)
                return errno; // not interrupted, return errno
        }
        else if (res == 0)
        {
            // empty write, should never happen, abort driver

            return bdus_abort;
        }
        else
        {
            // successfully wrote some data

            buffer += res;
            offset += (uint64_t)res;
            size -= (uint32_t)res;
        }
    }

    // success

    return 0;
}

static int device_write_zeros(
    uint64_t offset, uint32_t size, bool may_unmap, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // issue ioctl to write zeros to underlying device

    uint64_t range[2] = { offset, (uint64_t)size };

    if (ioctl(fd, BLKZEROOUT, range) != 0)
        return errno; // failed, return errno

    // success

    return 0;
}

static int device_flush(struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // flush entire underlying device

    if (fdatasync(fd) != 0)
        return errno; // failed, return errno

    // success

    return 0;
}

static int device_discard(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // issue ioctl to discard data from underlying device

    uint64_t range[2] = { offset, (uint64_t)size };

    if (ioctl(fd, BLKDISCARD, range) != 0)
        return errno; // failed, return errno

    // success

    return 0;
}

static int
    device_secure_erase(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // issue ioctl to securely erase data from underlying device

    uint64_t range[2] = { offset, (uint64_t)size };

    if (ioctl(fd, BLKSECDISCARD, range) != 0)
        return errno; // failed, return errno

    // success

    return 0;
}

static int device_ioctl(uint32_t command, void *argument, struct bdus_ctx *ctx)
{
    int fd = *(int *)ctx->private_data;

    // issue same ioctl to underlying device

    int result = ioctl(fd, (unsigned long)command, argument);

    if (result == -1)
        return errno; // error, return errno

    // success

    return 0;
}

// the discard and secure_erase operations are filled in by configure_device()
static const struct bdus_ops device_ops = {
    .read        = device_read,
    .write       = device_write,
    .write_zeros = device_write_zeros,
    .flush       = device_flush,
    .ioctl       = device_ioctl,
};

// the size, logical_block_size, and physical_block_size attributes are filled
// in by configure_device()
static const struct bdus_attrs device_attrs = {
    .max_concurrent_callbacks = 16, // enable parallel request processing
};

/* -------------------------------------------------------------------------- */

static int open_underlying_device(const char *file_path)
{
    // use O_DIRECT to avoid redundant caching

    int fd = open(file_path, O_RDWR | O_DIRECT);

    if (fd < 0)
    {
        fprintf(
            stderr, "Error: Failed to open underlying device (%s).\n",
            strerror(errno));
    }

    return fd;
}

static bool configure_device_discard(int fd, struct bdus_ops *ops)
{
    // determine if underlying device supports discard by submitting an empty
    // request and inspecting the resulting error

    uint64_t range[2] = { 0, 0 };

    if (ioctl(fd, BLKDISCARD, range) == 0 || errno == EINVAL)
    {
        // device supports discard
        ops->discard = device_discard;
        return true;
    }
    else if (errno == EOPNOTSUPP)
    {
        // device does not support discard
        ops->discard = NULL;
        return true;
    }
    else
    {
        // some unexpected error occurred
        return false;
    }
}

static bool configure_device_secure_erase(int fd, struct bdus_ops *ops)
{
    // determine if underlying device supports secure erase by submitting an
    // empty request and inspecting the resulting error

    uint64_t range[2] = { 0, 0 };

    if (ioctl(fd, BLKSECDISCARD, range) == 0 || errno == EINVAL)
    {
        // device supports secure erase
        ops->secure_erase = device_secure_erase;
        return true;
    }
    else if (errno == EOPNOTSUPP)
    {
        // device does not support secure erase
        ops->secure_erase = NULL;
        return true;
    }
    else
    {
        // some unexpected error occurred
        return false;
    }
}

// sets the size, logical block size, and physical block size attributes to
// match those of the given file descriptor, which must refer to a block device
static bool
    configure_device(int fd, struct bdus_ops *ops, struct bdus_attrs *attrs)
{
    // support discard / secure erase only if underlying device does

    if (!configure_device_discard(fd, ops))
        return false;

    if (!configure_device_secure_erase(fd, ops))
        return false;

    // mirror the size, logical block size, and physical block size of the
    // underlying device

    if (ioctl(fd, BLKGETSIZE64, &attrs->size) != 0)
        return false;

    if (ioctl(fd, BLKSSZGET, &attrs->logical_block_size) != 0)
        return false;

    if (ioctl(fd, BLKPBSZGET, &attrs->physical_block_size) != 0)
        return false;

    // success

    return true;
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    // validate usage

    if (argc != 2 && argc != 3)
    {
        fprintf(
            stderr, "Usage: %s <block_device> [<existing_dev_path>]\n",
            argv[0]);

        return 2;
    }

    // open underlying device

    int fd = open_underlying_device(argv[1]);

    if (fd < 0)
        return 1;

    // configure device from metadata about underlying device

    struct bdus_ops ops     = device_ops;
    struct bdus_attrs attrs = device_attrs;

    if (!configure_device(fd, &ops, &attrs))
    {
        fprintf(
            stderr,
            "Error: ioctl on underlying device failed. Is \"%s\" a block"
            " special file?\n",
            argv[1]);

        close(fd); // close underlying device
        return 1;
    }

    // run driver

    bool success;

    if (argc == 2)
    {
        // create new device and run driver

        success = bdus_run(&ops, &attrs, &fd);
    }
    else // argc == 3
    {
        // parse path of existing device

        uint64_t dev_id;

        if (bdus_get_dev_id_from_path(&dev_id, argv[2]))
        {
            // attach to existing device and run driver

            success = bdus_rerun(dev_id, &ops, &attrs, &fd);
        }
        else
        {
            success = false;
        }
    }

    // close underlying device

    close(fd);

    // print error message if driver failed

    if (!success)
        fprintf(stderr, "Error: %s\n", bdus_get_error_message());

    // exit with appropriate exit code

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
