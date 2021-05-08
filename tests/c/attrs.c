/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// This test ensures that several valid (invalid) attribute configurations are
// accepted (rejected).

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */

static int device_initialize(struct bdus_ctx *ctx)
{
    *(bool *)ctx->private_data = true;

    return EIO;
}

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_write(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_write_same(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_write_zeros(
    uint64_t offset, uint32_t size, bool may_unmap, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_fua_write(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_flush(struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_discard(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int
    device_secure_erase(uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return EIO;
}

static int device_ioctl(uint32_t command, void *argument, struct bdus_ctx *ctx)
{
    return EIO;
}

static const struct bdus_ops ops = {
    .initialize   = device_initialize,
    .read         = device_read,
    .write        = device_write,
    .write_same   = device_write_same,
    .write_zeros  = device_write_zeros,
    .fua_write    = device_fua_write,
    .flush        = device_flush,
    .discard      = device_discard,
    .secure_erase = device_secure_erase,
    .ioctl        = device_ioctl,
};

static const struct bdus_attrs good_attrs[] = {
    {
        .size               = 512,
        .logical_block_size = 512,
    },
    {
        .size                       = UINT64_MAX - 4095, // 16 EiB - 4 KiB
        .logical_block_size         = 4096,
        .physical_block_size        = 4096,
        .max_read_write_size        = UINT32_MAX,
        .max_write_same_size        = UINT32_MAX,
        .max_write_zeros_size       = UINT32_MAX,
        .max_discard_erase_size     = UINT32_MAX,
        .max_concurrent_callbacks   = UINT32_MAX,
        .disable_partition_scanning = true,
        .log                        = true,
    },
};

static const struct bdus_attrs bad_attrs[] = {
    { 0 },
    {
        .size               = 513,
        .logical_block_size = 513,
    },
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    // attrs that should pass

    for (size_t i = 0; i < sizeof(good_attrs) / sizeof(good_attrs[0]); ++i)
    {
        bool initialized = false;

        struct bdus_attrs attrs = good_attrs[i];
        attrs.dont_daemonize    = true;

        if (bdus_run(&ops, &attrs, &initialized))
            return 1;

        if (!initialized)
            return 1;
    }

    // attrs that should fail

    for (size_t i = 0; i < sizeof(bad_attrs) / sizeof(bad_attrs[0]); ++i)
    {
        bool initialized = false;

        struct bdus_attrs attrs = bad_attrs[i];
        attrs.dont_daemonize    = true;

        if (bdus_run(&ops, &attrs, &initialized))
            return 1;

        if (initialized)
            return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
