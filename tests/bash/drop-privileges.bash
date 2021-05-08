# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that a driver continues to function after dropping superuser
# privileges in its initialize() callback.

# ---------------------------------------------------------------------------- #

driver='
    #include <bdus.h>

    #include <errno.h>
    #include <pwd.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>
    #include <sys/types.h>
    #include <unistd.h>

    static int device_initialize(struct bdus_ctx *ctx)
    {
        struct passwd *pwd = ctx->private_data;

        // set uid and gid to those of user "nobody" to drop privileges

        if (setgid(pwd->pw_gid) != 0)
            return errno;

        if (setuid(pwd->pw_uid) != 0)
            return errno;

        return 0;
    }

    static int device_read(
        char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        memset(buffer, 0, (size_t)size);
        return 0;
    }

    static const struct bdus_ops device_ops = {
        .initialize = device_initialize,
        .read       = device_read,
    };

    static const struct bdus_attrs device_attrs = {
        .size               = 1 << 30,
        .logical_block_size = 512,
    };

    int main(void)
    {
        // get "nobody" user info

        errno = 0;

        struct passwd *pwd = getpwnam("nobody");

        if (!pwd)
        {
            if (errno == 0)
                fprintf(stderr, "User \"nobody\" not found.");
            else
                fprintf(stderr, "getpwnam() failed: %s", strerror(errno));

            return 1;
        }

        // run driver

        const bool success = bdus_run(&device_ops, &device_attrs, pwd);

        if (!success)
            fprintf(stderr, "Error: %s\n", bdus_get_error_message());

        return success ? 0 : 1;
    }
    '

# create device

device_path="$( run_c driver )"

# ensure that driver can handle requests after dropping privileges

dd if="${device_path}" iflag=direct of=/dev/null bs=1M count=1

# ---------------------------------------------------------------------------- #
