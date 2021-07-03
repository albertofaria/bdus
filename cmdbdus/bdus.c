/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _POSIX_C_SOURCE 199309L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <bdus.h>

/* -------------------------------------------------------------------------- */
/* main */

static const char *const usage =
    "Usage: bdus <subcommand> [<options...>] <args...>\n"
    "Try `bdus --help` for more information.\n";

static const char *const help =
    "USAGE\n"
    "   bdus <subcommand> [<options...>] <args...>\n"
    "\n"
    "DESCRIPTION\n"
    "   Manage devices created using BDUS, a framework for developing Block\n"
    "   Devices in User Space (https://github.com/albertofaria/bdus).\n"
    "\n"
    "   Try `bdus <subcommand> --help` for more information on a subcommand.\n"
    "\n"
    "SUBCOMMANDS\n"
    "   destroy   Destroy a device.\n"
    "   version   Print version information.\n";

static int subcommand_destroy(char **args, int num_args);
static int subcommand_version(char **args, int num_args);

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
    {
        fputs(help, stdout);
        return 0;
    }
    else if (argc >= 2 && strcmp(argv[1], "destroy") == 0)
    {
        return subcommand_destroy(argv + 2, argc - 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "version") == 0)
    {
        return subcommand_version(argv + 2, argc - 2);
    }
    else
    {
        fputs(usage, stderr);
        return 2;
    }
}

/* -------------------------------------------------------------------------- */
/* subcommand "destroy" */

static const char *const usage_destroy =
    "Usage: bdus destroy [<options...>] <dev_path_or_id>\n"
    "Try `bdus destroy --help` for more information.\n";

static const char *const help_destroy =
    "USAGE\n"
    "   bdus destroy [<options...>] <dev_path_or_id>\n"
    "\n"
    "DESCRIPTION\n"
    "   Destroy a device, ensuring that data previously written to it is\n"
    "   persistently stored beforehand.\n"
    "\n"
    "   If the identifier of a device that no longer exists is specified, the\n"
    "   device is not flushed and this command immediately succeeds.\n"
    "\n"
    "ARGUMENTS\n"
    "   <dev_path_or_id>   Path to, or identifier of, the device to destroy.\n"
    "\n"
    "OPTIONS\n"
    "   -q, --quiet   Print only error messages.\n"
    "   --no-flush    Don't flush previously written data.\n";

#define SECONDS_UNTIL_UNRESPONSIVE_FLUSH_MESSAGE 3

static const char *const unresponsive_flush_message =
    "(The flush request has not yet been completed. Rerun this command with\n"
    "flag --no-flush to forcefully destroy the device *without* ensuring that\n"
    "written data is persistently stored.)\n";

static bool parse_dev_id(uint64_t *out_value, const char *str)
{
    if (str[0] == '\0' || str[0] == '+' || str[0] == '-' || isspace(str[0]))
        return false; // empty string or starts with a sign or whitespace char

    char *str_end;
    const unsigned long long value = strtoull(str, &str_end, 10);

    if (value == ULLONG_MAX)
        return false; // parsing failed

    if (str_end[0] != '\0')
        return false; // didn't consume entire string

    *out_value = (uint64_t)value;
    return true;
}

struct flush_timeout_state
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool done;
};

static void *flush_timeout_fn(void *arg)
{
    struct flush_timeout_state *const state = arg;

    // compute timeout time

    struct timespec abstime;

    if (clock_gettime(CLOCK_REALTIME, &abstime) != 0)
        abort();

    abstime.tv_sec += SECONDS_UNTIL_UNRESPONSIVE_FLUSH_MESSAGE;

    // wait until device is destroyed or timeout elapses

    if (pthread_mutex_lock(&state->mutex) != 0)
        abort();

    int err = 0;

    while (!state->done && err == 0)
        err = pthread_cond_timedwait(&state->cond, &state->mutex, &abstime);

    if (pthread_mutex_unlock(&state->mutex) != 0)
        abort();

    // print message if timeout elapsed

    if (err == ETIMEDOUT)
        fputs(unresponsive_flush_message, stdout);

    return NULL;
}

static int flush_dev_with_timeout_message(uint64_t dev_id, bool quiet)
{
    if (!quiet)
    {
        puts("Flushing device...");
        fflush(stdout); // ensure message is printed immediately
    }

    // start timeout thread

    struct flush_timeout_state state = { .done = false };
    pthread_t thread;

    if (pthread_mutex_init(&state.mutex, NULL) != 0
        || pthread_cond_init(&state.cond, NULL) != 0
        || pthread_create(&thread, NULL, flush_timeout_fn, &state) != 0)
    {
        abort();
    }

    // flush device

    errno = 0;
    bdus_flush_dev(dev_id);

    const int result = errno;

    // stop timeout thread

    if (pthread_mutex_lock(&state.mutex) != 0)
        abort();

    state.done = true;

    if (pthread_cond_signal(&state.cond) != 0
        || pthread_mutex_unlock(&state.mutex) != 0
        || pthread_join(thread, NULL) != 0
        || pthread_cond_destroy(&state.cond) != 0
        || pthread_mutex_destroy(&state.mutex) != 0)
    {
        abort();
    }

    // return result

    if (result == ENODEV && !quiet)
        puts("The device no longer exists.");

    return result;
}

static int subcommand_destroy(char **args, int num_args)
{
    // parse arguments

    if (num_args == 1 && strcmp(args[0], "--help") == 0)
    {
        fputs(help_destroy, stdout);
        return 0;
    }

    const char *dev_path_or_id = NULL;

    bool quiet     = false;
    bool flush_dev = true;

    for (int i = 0; i < num_args; ++i)
    {
        if (strcmp(args[i], "-q") == 0 || strcmp(args[i], "--quiet") == 0)
        {
            quiet = true;
        }
        else if (strcmp(args[i], "--no-flush") == 0)
        {
            flush_dev = false;
        }
        else if (!dev_path_or_id)
        {
            dev_path_or_id = args[i];
        }
        else
        {
            fputs(usage_destroy, stderr);
            return 2;
        }
    }

    if (!dev_path_or_id)
    {
        fputs(usage_destroy, stderr);
        return 2;
    }

    // get device id

    uint64_t dev_id;

    bool success = parse_dev_id(&dev_id, dev_path_or_id)
        || bdus_get_dev_id_from_path(&dev_id, dev_path_or_id);

    // flush and destroy device

    bool destroy_dev = true;

    if (success && flush_dev)
    {
        const int result = flush_dev_with_timeout_message(dev_id, quiet);

        if (result == ENODEV)
            destroy_dev = false;
        else if (result != 0)
            success = false;
    }

    if (success && destroy_dev)
    {
        if (!quiet)
        {
            puts("Destroying device...");
            fflush(stdout); // ensure message is printed immediately
        }

        success = bdus_destroy_dev(dev_id) || errno == ENODEV;
    }

    // check errors

    if (!success)
    {
        fprintf(stderr, "Error: %s\n", bdus_get_error_message());
        return 1;
    }

    // success

    if (!quiet)
        puts("Done.");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* subcommand "version" */

static const char *const usage_version =
    "Usage: bdus version [<options...>]\n"
    "Try `bdus version --help` for more information.\n";

static const char *const help_version =
    "USAGE\n"
    "   bdus version [<options...>]\n"
    "\n"
    "DESCRIPTION\n"
    "   Print the versions of installed BDUS components.\n"
    "\n"
    "   If no options are given, the versions of this command, libbdus, and\n"
    "   kbdus are printed.\n"
    "\n"
    "OPTIONS\n"
    "   --cmdbdus   Print the version of this command.\n"
    "   --libbdus   Print the version of libbdus in use.\n"
    "   --kbdus     Print the version of kbdus.\n";

static int subcommand_version(char **args, int num_args)
{
    // parse arguments

    if (num_args == 1 && strcmp(args[0], "--help") == 0)
    {
        fputs(help_version, stdout);
        return 0;
    }

    bool print_cmdbdus = false;
    bool print_libbdus = false;
    bool print_kbdus   = false;

    for (int i = 0; i < num_args; ++i)
    {
        if (strcmp(args[i], "--cmdbdus") == 0)
        {
            print_cmdbdus = true;
        }
        else if (strcmp(args[i], "--libbdus") == 0)
        {
            print_libbdus = true;
        }
        else if (strcmp(args[i], "--kbdus") == 0)
        {
            print_kbdus = true;
        }
        else
        {
            fputs(usage_version, stderr);
            return 2;
        }
    }

    if (!print_cmdbdus && !print_libbdus && !print_kbdus)
    {
        print_cmdbdus = true;
        print_libbdus = true;
        print_kbdus   = true;
    }

    // print cmdbdus version

    if (print_cmdbdus)
    {
        fprintf(
            stdout, "cmdbdus %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
            (uint32_t)BDUS_HEADER_VERSION_MAJOR,
            (uint32_t)BDUS_HEADER_VERSION_MINOR,
            (uint32_t)BDUS_HEADER_VERSION_PATCH);
    }

    // print libbdus version

    if (print_libbdus)
    {
        const struct bdus_version *libbdus_ver = bdus_get_libbdus_version();

        fprintf(
            stdout, "libbdus %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
            libbdus_ver->major, libbdus_ver->minor, libbdus_ver->patch);
    }

    // print kbdus version

    if (print_kbdus)
    {
        struct bdus_version kbdus_ver;

        if (!bdus_get_kbdus_version(&kbdus_ver))
        {
            fflush(stdout); // ensure versions appear before error

            fprintf(
                stderr, "Error: Failed to get kbdus version: %s\n",
                bdus_get_error_message());

            return 1;
        }

        fprintf(
            stdout, "kbdus   %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
            kbdus_ver.major, kbdus_ver.minor, kbdus_ver.patch);
    }

    // success

    return 0;
}

/* -------------------------------------------------------------------------- */
