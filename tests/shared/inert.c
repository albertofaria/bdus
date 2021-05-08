/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <stdbool.h>
#include <stdio.h>

/* -------------------------------------------------------------------------- */

static const struct bdus_ops device_ops = { 0 };

static const struct bdus_attrs device_attrs = {
    .size               = 1 << 30,
    .logical_block_size = 512,
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
