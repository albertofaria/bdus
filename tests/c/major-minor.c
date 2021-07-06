/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

// This test ensures that the major and minor device numbers available through
// struct bdus_ctx match those of the device's block special file.

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

/* -------------------------------------------------------------------------- */

static int device_on_device_available(struct bdus_ctx *ctx)
{
    struct stat st;

    if (stat(ctx->path, &st) != 0)
        return errno;

    if (ctx->major == major(st.st_rdev) && ctx->minor == minor(st.st_rdev))
        *(bool *)ctx->private_data = true;

    return EIO;
}

static const struct bdus_ops device_ops = {
    .on_device_available = device_on_device_available,
};

static const struct bdus_attrs device_attrs = {
    .size               = 1 << 30,
    .logical_block_size = 512,
    .dont_daemonize     = true,
};

/* -------------------------------------------------------------------------- */

int main(void)
{
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        bool matches = false;

        bdus_run(&device_ops, &device_attrs, &matches);

        if (!matches)
            return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
