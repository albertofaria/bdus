/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// The driver featured in the Quick Start Guide, implementing a 1 GiB RAM-based
// volatile device.

// Compile with:
//     cc ram.c -lbdus -o ram

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    memcpy(buffer, (char *)ctx->private_data + offset, size);
    return 0;
}

static int device_write(
    const char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    memcpy((char *)ctx->private_data + offset, buffer, size);
    return 0;
}

static const struct bdus_ops device_ops = {
    .read  = device_read,
    .write = device_write,
};

static const struct bdus_attrs device_attrs = {
    .size               = 1 << 30, // 1 GiB
    .logical_block_size = 512,
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    void *buffer = malloc(device_attrs.size);

    if (!buffer)
        return 1;

    bool success = bdus_run(&device_ops, &device_attrs, buffer);

    free(buffer);

    if (!success)
        fprintf(stderr, "Error: %s\n", bdus_get_error_message());

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
