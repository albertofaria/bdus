/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _XOPEN_SOURCE 500

#include <bdus.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */

#define BDUS_MAX_DELAY_US 2000llu

static void device_sleep(void)
{
    usleep((unsigned long long)rand() * BDUS_MAX_DELAY_US / RAND_MAX);
}

/* -------------------------------------------------------------------------- */

static int device_initialize(struct bdus_ctx *ctx)
{
    ctx->private_data = malloc((size_t)ctx->attrs->size);
    return ctx->private_data ? 0 : ENOMEM;
}

static int device_terminate(struct bdus_ctx *ctx)
{
    free(ctx->private_data);
    return 0;
}

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    __sync_synchronize();
    memcpy(buffer, (char *)ctx->private_data + offset, (size_t)size);

    device_sleep();

    return 0;
}

static int device_write(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    device_sleep();

    memcpy((char *)ctx->private_data + offset, buffer, (size_t)size);
    __sync_synchronize();

    return 0;
}

static int device_write_same(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    device_sleep();

    uint32_t lbs = ctx->attrs->logical_block_size;

    for (uint64_t i = offset; i < offset + size; i += (uint64_t)lbs)
        memcpy((char *)ctx->private_data + i, buffer, (size_t)lbs);

    __sync_synchronize();

    return 0;
}

static int device_write_zeros(
    uint64_t offset, uint32_t size, bool may_unmap, struct bdus_ctx *ctx)
{
    device_sleep();

    memset((char *)ctx->private_data + offset, 0, (size_t)size);
    __sync_synchronize();

    return 0;
}

static int device_flush(struct bdus_ctx *ctx)
{
    device_sleep();

    return 0;
}

static int device_discard(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    device_write_zeros(offset, size, true, ctx);

    return 0;
}

static int
    device_secure_erase(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    device_write_zeros(offset, size, true, ctx);

    return 0;
}

#define TEST_IOCTL_NONE _IO(42, 100)
#define TEST_IOCTL_READ _IOR(42, 101, uint64_t)
#define TEST_IOCTL_WRITE _IOW(42, 102, uint64_t)
#define TEST_IOCTL_READ_WRITE _IOWR(42, 103, uint64_t)

static int device_ioctl(uint32_t command, void *argument, struct bdus_ctx *ctx)
{
    device_sleep();

    uint64_t *const arg = argument;

    switch (command)
    {
    case TEST_IOCTL_NONE:
        return 0;

    case TEST_IOCTL_READ:

        if (*arg != 1234)
            return EINVAL;

        return 0;

    case TEST_IOCTL_WRITE:

        if (*arg != 0)
            return EIO;

        *arg = 2345;

        return 0;

    case TEST_IOCTL_READ_WRITE:

        if (*arg != 1234)
            return EINVAL;

        *arg = 2345;

        return 0;

    default:
        return ENOTTY;
    }
}

static const struct bdus_ops device_ops = {
    .initialize   = device_initialize,
    .terminate    = device_terminate,
    .read         = device_read,
    .write        = device_write,
    .write_same   = device_write_same,
    .write_zeros  = device_write_zeros,
    .flush        = device_flush,
    .discard      = device_discard,
    .secure_erase = device_secure_erase,
    .ioctl        = device_ioctl,
};

static const struct bdus_attrs device_attrs = {
    .size                     = 1 << 30, // 1 GiB
    .logical_block_size       = 512,
    .max_concurrent_callbacks = 8,
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    const bool success = bdus_run(&device_ops, &device_attrs, NULL);

    if (!success)
        fprintf(stderr, "Error: %s\n", bdus_get_error_message());

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
