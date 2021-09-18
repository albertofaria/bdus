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
        return ENOSPC;
    }

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return ENOLINK;
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
    #include <inttypes.h>
    #include <linux/fs.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>

    #include <bdus.h>

    char buffer[512] __attribute__((aligned(512)));
    uint64_t range[2] = { 0, 512 };

    #define expect(expr, ret) \
        do \
        { \
            const int actual_ret = (expr); \
             \
            if (actual_ret != (ret)) \
            { \
                fprintf(stderr, "ERROR: %s evaluated to %d, expected %d\n", \
                    #expr, actual_ret, (ret)); \
                exit(1); \
            } \
        } \
        while (0)

    #define expect_errno(expr, ret, err) \
        do \
        { \
            expect(expr, ret); \
              \
            if (errno != (err)) \
            { \
                fprintf(stderr, "ERROR: %s resulted in errno %s, expected %s\n", \
                    #expr, strerror(errno), strerror((err))); \
                exit(1); \
            } \
        } \
        while (0)

    void expect_dev_size(int fd, uint64_t expected_size)
    {
        uint64_t actual_size;

        if (ioctl(fd, BLKGETSIZE64, &actual_size) != 0)
            exit(1);

        if (expected_size != actual_size)
        {
            fprintf(stderr,
                "ERROR: Device has size %"PRIu64", expected %"PRIu64".\n",
                actual_size, expected_size);
            exit(1);
        }
    }

    void check_errno_before_destroying(int fd)
    {
        if ('"$( kernel_is_at_least 4.10 && echo true || echo false )"')
        {
            expect_errno(pread(fd, buffer, sizeof(buffer), 0), -1, ENOSPC);
            expect_errno(pwrite(fd, buffer, sizeof(buffer), 0), -1, ENOLINK);
        }
        else
        {
            expect_errno(pread(fd, buffer, sizeof(buffer), 0), -1, EIO);
            expect_errno(pwrite(fd, buffer, sizeof(buffer), 0), -1, EIO);
        }

        if ('"$( kernel_is_at_least 4.3 && echo true || echo false )"')
            expect_errno(ioctl(fd, BLKDISCARD, range), -1, ETIMEDOUT);
        else
            expect_errno(ioctl(fd, BLKDISCARD, range), -1, EIO);

        expect_errno(fsync(fd), -1, EIO);
        expect_errno(ioctl(fd, _IO(42, 100)), -1, EOVERFLOW);
    }

    void check_errno_after_destroying(int fd)
    {
        if ('"$( kernel_is_at_least 5.11 && echo true || echo false )"')
        {
            sleep(2);  // can take a bit for device to appear empty

            expect_dev_size(fd, 0);

            expect(pread(fd, buffer, sizeof(buffer), 0), 0);
            expect_errno(pwrite(fd, buffer, sizeof(buffer), 0), -1, ENOSPC);
            expect_errno(ioctl(fd, BLKDISCARD, range), -1, EINVAL);
        }
        else
        {
            expect_dev_size(fd, 1 << 30);

            expect_errno(pread(fd, buffer, sizeof(buffer), 0), -1, EIO);
            expect_errno(pwrite(fd, buffer, sizeof(buffer), 0), -1, EIO);
            expect_errno(ioctl(fd, BLKDISCARD, range), -1, EIO);
        }

        expect_errno(fsync(fd), -1, EIO);
        expect_errno(ioctl(fd, _IO(42, 100)), -1, ENODEV);
    }

    int main(int argc, char **argv)
    {
        uint64_t dev_id;

        if (!bdus_get_dev_id_from_path(&dev_id, argv[1]))
            return 1;

        const int fd = open(argv[1], O_RDWR | O_DIRECT | O_EXCL);

        if (fd < 0)
            return 1;

        check_errno_before_destroying(fd);

        if (!bdus_destroy_dev(dev_id))
            return 1;

        check_errno_after_destroying(fd);

        return 0;
    }
    '

# create device and check errors

device_path="$( run_c driver )"

run_c check_errno "${device_path}"

# ---------------------------------------------------------------------------- #
