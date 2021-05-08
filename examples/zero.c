/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// The driver for a read-only, zero-filled 1 GiB device.

// This driver can both create a new device (by running the driver executable
// with no arguments) or replace an existing device's driver (by specifying a
// path to that device as an argument).

// Compile with:
//     cc zero.c -lbdus -o zero

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

static int device_read(
    char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx)
{
    memset(buffer, 0, (size_t)size); // zero-fill request buffer
    return 0; // success
}

static const struct bdus_ops device_ops = {
    .read = device_read,
};

static const struct bdus_attrs device_attrs = {
    .size                     = 1 << 30, // 1 GiB
    .logical_block_size       = 512,
    .max_concurrent_callbacks = 16, // enable parallel request processing
};

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    bool success;

    if (argc == 1)
    {
        // create new device and run driver

        success = bdus_run(&device_ops, &device_attrs, NULL);
    }
    else if (argc == 2)
    {
        // get id of existing device

        uint64_t dev_id;

        if (bdus_get_dev_id_from_path(&dev_id, argv[1]))
        {
            // attach to existing device and run driver

            success = bdus_rerun(dev_id, &device_ops, &device_attrs, NULL);
        }
        else
        {
            success = false;
        }
    }
    else
    {
        fprintf(stderr, "Usage: %s [<existing_dev_path>]\n", argv[0]);
        return 2;
    }

    // print error message if driver failed

    if (!success)
        fprintf(stderr, "Error: %s\n", bdus_get_error_message());

    // exit with appropriate exit code

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
