# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that requests can be timed out.

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
        memset(buffer, 0, (size_t)size);
        return 0;
    }

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        (void)usleep(4000000);
        return EIO;
    }

    static int device_flush(struct bdus_ctx *ctx)
    {
        (void)usleep(4000000);
        return EIO;
    }

    static int device_ioctl(
        uint32_t command, void *argument, struct bdus_ctx *ctx)
    {
        (void)usleep(4000000);
        return EIO;
    }

    static const struct bdus_ops device_ops = {
        .read  = device_read,
        .write = device_write,
        .flush = device_flush,
        .ioctl = device_ioctl,
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

submit_ioctl='
    #define _GNU_SOURCE
    #include <errno.h>
    #include <fcntl.h>
    #include <stdbool.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <sys/types.h>

    #include <kbdus.h>

    int main(int argc, char **argv)
    {
        const int fd = open(argv[1], O_RDWR | O_DIRECT | O_EXCL);

        const bool success =
            fd >= 0
            && ioctl(fd, KBDUS_IOCTL_GET_VERSION) != 0
            && errno == ETIMEDOUT;

        return success ? 0 : 1;
    }
    '

# create device

device_path="$( run_c driver )"

# skip test if kernel has no io_timeout sysfs attribute

io_timeout_path="/sys/block/$( basename "${device_path}" )/queue/io_timeout"

[[ -e "${io_timeout_path}" ]] || exit 0

# set device timeout

echo 2000 > "${io_timeout_path}"

# submit read request expecting success

dd if="${device_path}" iflag=direct of=/dev/zero bs=512 count=1

# submit write, flush, and ioctl requests expecting timeout

{ ! dd if=/dev/zero of="${device_path}" oflag=direct bs=512 count=1; } |&
    grep "Connection timed out"

{ ! sync "${device_path}"; } |& grep "Connection timed out"

run_c submit_ioctl "${device_path}"

# ---------------------------------------------------------------------------- #
