# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that errno values are correctly propagated from request
# handlers to clients, and that requests submitted to destroyed devices fail
# with the correct errno values.

# ---------------------------------------------------------------------------- #

driver='
    #define _DEFAULT_SOURCE
    #include <errno.h>
    #include <stdint.h>
    #include <string.h>
    #include <unistd.h>

    #include <bdus.h>

    static int device_read(
        char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return ENOLINK;
    }

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return ENOSPC;
    }

    static int device_discard(
        uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return ETIMEDOUT;
    }

    static int device_flush(struct bdus_ctx *ctx)
    {
        return EINVAL;
    }

    static int device_ioctl(
        uint32_t command, void *argument, struct bdus_ctx *ctx)
    {
        return EOVERFLOW;
    }

    static const struct bdus_ops device_ops = {
        .read    = device_read,
        .write   = device_write,
        .discard = device_discard,
        .flush   = device_flush,
        .ioctl   = device_ioctl,
    };

    static const struct bdus_attrs device_attrs = {
        .size                     = 1 << 30,
        .logical_block_size       = 512,
        .max_concurrent_callbacks = 16,
    };

    int main(void)
    {
        return bdus_run(&device_ops, &device_attrs, NULL) ? 0 : 1;
    }
    '

check_errno='
    #define _GNU_SOURCE
    #include <errno.h>
    #include <fcntl.h>
    #include <linux/fs.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>

    #include <bdus.h>
    #include <kbdus.h>

    char buffer[512] __attribute__((aligned(512)));
    uint64_t range[2] = { 0, 512 };

    bool check_errno_before_destroying(int fd)
    {
        bool success = true;

        success =
            success
            && pread(fd, buffer, sizeof(buffer), 0) == -1
            && errno == ENOLINK;

        success =
            success
            && pwrite(fd, buffer, sizeof(buffer), 0) == -1
            && errno == ENOSPC;

        success =
            success
            && ioctl(fd, BLKDISCARD, range) == -1
            && errno == ETIMEDOUT;

        success =
            success
            && fsync(fd) == -1
            && errno == EIO;

        success =
            success
            && ioctl(fd, _IO(42, 100)) == -1
            && errno == EOVERFLOW;

        return success;
    }

    bool check_errno_after_destroying(int fd)
    {
        bool success = true;

        success =
            success
            && pread(fd, buffer, sizeof(buffer), 0) == -1
            && errno == EIO;

        success =
            success
            && pwrite(fd, buffer, sizeof(buffer), 0) == -1
            && errno == EIO;

        success =
            success
            && ioctl(fd, BLKDISCARD, range) == -1
            && errno == EIO;

        success =
            success
            && fsync(fd) == -1
            && errno == EIO;

        success =
            success
            && ioctl(fd, _IO(42, 100)) == -1
            && errno == ENODEV;

        return success;
    }

    int main(int argc, char **argv)
    {
        uint64_t dev_id;

        if (!bdus_get_dev_id_from_path(&dev_id, argv[1]))
            return 1;

        const int fd = open(argv[1], O_RDWR | O_DIRECT | O_EXCL);

        if (fd < 0)
            return 1;

        if (!check_errno_before_destroying(fd))
            return 1;

        if (!bdus_destroy_dev(dev_id))
            return 1;

        if (!check_errno_after_destroying(fd))
            return 1;

        return 0;
    }
    '

# create device and check errors

device_path="$( run_c driver )"

run_c check_errno "${device_path}"

# ---------------------------------------------------------------------------- #
