# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that:
#
#   - If a driver tries to attach to a *non-recoverable* device that already has
#     a controlling driver, and if the latter's flush() or terminate() callbacks
#     fail, then the former *fails* to attach to the device;
#
#   - If a driver tries to attach to a *recoverable* device that already has a
#     controlling driver, and if the latter's flush() or terminate() callbacks
#     fail, then the former *successfully* attaches to the device.

# ---------------------------------------------------------------------------- #

driver_fail_flush='
    #include <bdus.h>

    #include <errno.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return 0;
    }

    static int device_flush(struct bdus_ctx *ctx)
    {
        return ctx->is_rerun ? 0 : EIO;
    }

    static const struct bdus_ops device_ops = {
        .write = device_write,
        .flush = device_flush,
    };

    static const struct bdus_attrs device_attrs = {
        .size               = 1 << 30,
        .logical_block_size = 512,
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
                && !bdus_rerun(dev_id, &device_ops, &device_attrs, NULL)
                && errno == ENODEV;
        }

        return success ? 0 : 1;
    }
    '

driver_fail_terminate='
    #include <bdus.h>

    #include <errno.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>

    static int device_terminate(struct bdus_ctx *ctx)
    {
        return ctx->is_rerun ? 0 : EIO;
    }

    static const struct bdus_ops device_ops = {
        .terminate = device_terminate,
    };

    static const struct bdus_attrs device_attrs = {
        .size               = 1 << 30,
        .logical_block_size = 512,
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
                && !bdus_rerun(dev_id, &device_ops, &device_attrs, NULL)
                && errno == ENODEV;
        }

        return success ? 0 : 1;
    }
    '

driver_recoverable_fail_flush='
    #include <bdus.h>

    #include <errno.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        return 0;
    }

    static int device_flush(struct bdus_ctx *ctx)
    {
        return ctx->is_rerun ? 0 : EIO;
    }

    static const struct bdus_ops device_ops = {
        .write = device_write,
        .flush = device_flush,
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

driver_recoverable_fail_terminate='
    #include <bdus.h>

    #include <errno.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>

    static int device_terminate(struct bdus_ctx *ctx)
    {
        return ctx->is_rerun ? 0 : EIO;
    }

    static const struct bdus_ops device_ops = {
        .terminate = device_terminate,
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

# create and replace devices

device="$( run_c driver_fail_flush )"
run_c driver_fail_flush "${device}"

device="$( run_c driver_fail_terminate )"
run_c driver_fail_terminate "${device}"

device="$( run_c driver_recoverable_fail_flush )"
run_c driver_recoverable_fail_flush "${device}"
bdus destroy --no-flush --quiet "${device}"

device="$( run_c driver_recoverable_fail_terminate )"
run_c driver_recoverable_fail_terminate "${device}"
bdus destroy --no-flush --quiet "${device}"

# ---------------------------------------------------------------------------- #
