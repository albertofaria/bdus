# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test tests the scenario in which (1) a discard request is submitted to a
# recoverable driver, (2) the driver fails while handling the request, (3) the
# driver is restarted, and (4) the request is retried and handled by the new
# driver, all transparently to the client that submitted the request.

# ---------------------------------------------------------------------------- #

driver='
    #include <bdus.h>

    #include <errno.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdlib.h>
    #include <string.h>

    static int device_discard(
        uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        if (!ctx->is_rerun)
            abort();

        return 0;
    }

    static const struct bdus_ops device_ops = {
        .discard = device_discard,
    };

    static const struct bdus_attrs device_attrs = {
        .size               = 1 << 30,
        .logical_block_size = 512,
        .recoverable        = true,
    };

    int main(int argc, char **argv)
    {
        bool success;

        if (argc == 1)
        {
            success = bdus_run(&device_ops, &device_attrs, NULL);
        }
        else
        {
            uint64_t dev_id;

            success =
                bdus_get_dev_id_from_path(&dev_id, argv[1])
                && bdus_rerun(dev_id, &device_ops, &device_attrs, NULL);
        }

        return success ? 0 : 1;
    }
    '

# compile driver

driver_binary="$( compile_c driver )"
trap '{ rm -f "${driver_binary}"; }' EXIT

# run driver

device_path="$( "${driver_binary}" )"
driver_pid="$( pgrep --full "${driver_binary}" )"

# submit discard request

blkdiscard --length 512 "${device_path}" &
discard_pid="$!"

# wait until driver dies and restart it

tail --pid="${driver_pid}" -f /dev/null
"${driver_binary}" "${device_path}"

# await discard request

wait "${discard_pid}"

# ---------------------------------------------------------------------------- #
