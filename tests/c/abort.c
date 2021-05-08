/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// This test ensures that a driver is terminated when a callback returns
// bdus_abort.

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    return bdus_abort;
}

static const struct bdus_ops device_ops = {
    .read = device_read,
};

static const struct bdus_attrs device_attrs = {
    .size               = 1 << 30,
    .logical_block_size = 512,
    .dont_daemonize     = true,
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    const bool success = !bdus_run(&device_ops, &device_attrs, NULL)
        && errno == EIO
        && strncmp(bdus_get_error_message(), "Driver aborted", 14) == 0;

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
