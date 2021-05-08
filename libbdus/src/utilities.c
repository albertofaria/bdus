/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _POSIX_C_SOURCE 200112L

#include <libbdus/utilities.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/* system calls */

int bdus_close_keep_errno_(int fd)
{
    const int previous_errno = errno;

    const int ret = close(fd);

    errno = previous_errno;

    return ret;
}

int bdus_open_retry_(const char *pathname, int flags)
{
    while (true)
    {
        const int ret = open(pathname, flags);

        if (ret != -1 || errno != EINTR)
            return ret;
    }
}

int bdus_ioctl_retry_(int fd, unsigned long request)
{
    while (true)
    {
        const int ret = ioctl(fd, request);

        if (ret != -1 || errno != EINTR)
            return ret;
    }
}

int bdus_ioctl_arg_retry_(int fd, unsigned long request, void *argp)
{
    while (true)
    {
        const int ret = ioctl(fd, request, argp);

        if (ret != -1 || errno != EINTR)
            return ret;
    }
}

size_t bdus_get_page_size_(void)
{
    const long page_size = sysconf(_SC_PAGE_SIZE);

    if (page_size < 0)
    {
        bdus_set_error_(errno, "sysconf(_SC_PAGE_SIZE) failed");
        return 0;
    }

    return (size_t)page_size;
}

/* -------------------------------------------------------------------------- */
/* redirection & daemonization */

bool bdus_redirect_to_dev_null_(int fd, int flags)
{
    const int new_fd = bdus_open_retry_("/dev/null", flags);

    if (new_fd < 0)
        return false;

    if (new_fd != fd)
    {
        const int ret = dup2(new_fd, fd);

        close(new_fd);

        if (ret < 0)
            return false;
    }

    return true;
}

bool bdus_daemonize_(void)
{
    // clear errno (because we might fail without setting it)

    errno = 0;

    // flush all output streams (necessary since we use _exit() and not exit())

    if (fflush(NULL) != 0)
        return false;

    // first fork

    const pid_t fork_result = fork();

    if (fork_result < 0)
    {
        // ERROR

        return false;
    }
    else if (fork_result != 0)
    {
        // IN PARENT

        // wait for child

        int wstatus;

        if (waitpid(fork_result, &wstatus, 0) != fork_result)
            return false;
        else if (!WIFEXITED(wstatus))
            return false;
        else if (WEXITSTATUS(wstatus) != 0)
            return false;
        else
            _exit(0);
    }
    else
    {
        // IN CHILD

        // become process group leader and session group leader

        if (setsid() == (pid_t)(-1))
            _exit(1);

        // redirect stdin, stdout, and stderr to /dev/null

        if (!bdus_redirect_to_dev_null_(STDIN_FILENO, O_RDONLY))
            _exit(1);

        if (!bdus_redirect_to_dev_null_(STDOUT_FILENO, O_WRONLY))
            _exit(1);

        if (!bdus_redirect_to_dev_null_(STDERR_FILENO, O_WRONLY))
            _exit(1);

        // fork

        const pid_t fork_result = fork();

        if (fork_result < 0)
        {
            // ERROR

            _exit(1);
        }
        else if (fork_result != 0)
        {
            // IN PARENT

            _exit(0);
        }
        else
        {
            // IN CHILD

            // done

            return true;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* errors */

static const char *bdus_errno_symbolic_name_(int errno_value)
{
    // clang-format off
    switch (errno_value)
    {
    case EPERM: return "EPERM";
    case ENOENT: return "ENOENT";
    case ESRCH: return "ESRCH";
    case EINTR: return "EINTR";
    case EIO: return "EIO";
    case ENXIO: return "ENXIO";
    case E2BIG: return "E2BIG";
    case ENOEXEC: return "ENOEXEC";
    case EBADF: return "EBADF";
    case ECHILD: return "ECHILD";
#if EAGAIN == EWOULDBLOCK
    case EAGAIN: return "EAGAIN / EWOULDBLOCK";
#else
    case EAGAIN: return "EAGAIN";
    case EWOULDBLOCK: return "EWOULDBLOCK";
#endif
    case ENOMEM: return "ENOMEM";
    case EACCES: return "EACCES";
    case EFAULT: return "EFAULT";
    case ENOTBLK: return "ENOTBLK";
    case EBUSY: return "EBUSY";
    case EEXIST: return "EEXIST";
    case EXDEV: return "EXDEV";
    case ENODEV: return "ENODEV";
    case ENOTDIR: return "ENOTDIR";
    case EISDIR: return "EISDIR";
    case EINVAL: return "EINVAL";
    case ENFILE: return "ENFILE";
    case EMFILE: return "EMFILE";
    case ENOTTY: return "ENOTTY";
    case ETXTBSY: return "ETXTBSY";
    case EFBIG: return "EFBIG";
    case ENOSPC: return "ENOSPC";
    case ESPIPE: return "ESPIPE";
    case EROFS: return "EROFS";
    case EMLINK: return "EMLINK";
    case EPIPE: return "EPIPE";
    case EDOM: return "EDOM";
    case ERANGE: return "ERANGE";
    case EDEADLK: return "EDEADLK";
    case ENAMETOOLONG: return "ENAMETOOLONG";
    case ENOLCK: return "ENOLCK";
    case ENOSYS: return "ENOSYS";
    case ENOTEMPTY: return "ENOTEMPTY";
    case ELOOP: return "ELOOP";
    case ENOMSG: return "ENOMSG";
    case EIDRM: return "EIDRM";
    case ECHRNG: return "ECHRNG";
    case EL2NSYNC: return "EL2NSYNC";
    case EL3HLT: return "EL3HLT";
    case EL3RST: return "EL3RST";
    case ELNRNG: return "ELNRNG";
    case EUNATCH: return "EUNATCH";
    case ENOCSI: return "ENOCSI";
    case EL2HLT: return "EL2HLT";
    case EBADE: return "EBADE";
    case EBADR: return "EBADR";
    case EXFULL: return "EXFULL";
    case ENOANO: return "ENOANO";
    case EBADRQC: return "EBADRQC";
    case EBADSLT: return "EBADSLT";
    case EBFONT: return "EBFONT";
    case ENOSTR: return "ENOSTR";
    case ENODATA: return "ENODATA";
    case ETIME: return "ETIME";
    case ENOSR: return "ENOSR";
    case ENONET: return "ENONET";
    case ENOPKG: return "ENOPKG";
    case EREMOTE: return "EREMOTE";
    case ENOLINK: return "ENOLINK";
    case EADV: return "EADV";
    case ESRMNT: return "ESRMNT";
    case ECOMM: return "ECOMM";
    case EPROTO: return "EPROTO";
    case EMULTIHOP: return "EMULTIHOP";
    case EDOTDOT: return "EDOTDOT";
    case EBADMSG: return "EBADMSG";
    case EOVERFLOW: return "EOVERFLOW";
    case ENOTUNIQ: return "ENOTUNIQ";
    case EBADFD: return "EBADFD";
    case EREMCHG: return "EREMCHG";
    case ELIBACC: return "ELIBACC";
    case ELIBBAD: return "ELIBBAD";
    case ELIBSCN: return "ELIBSCN";
    case ELIBMAX: return "ELIBMAX";
    case ELIBEXEC: return "ELIBEXEC";
    case EILSEQ: return "EILSEQ";
    case ERESTART: return "ERESTART";
    case ESTRPIPE: return "ESTRPIPE";
    case EUSERS: return "EUSERS";
    case ENOTSOCK: return "ENOTSOCK";
    case EDESTADDRREQ: return "EDESTADDRREQ";
    case EMSGSIZE: return "EMSGSIZE";
    case EPROTOTYPE: return "EPROTOTYPE";
    case ENOPROTOOPT: return "ENOPROTOOPT";
    case EPROTONOSUPPORT: return "EPROTONOSUPPORT";
    case ESOCKTNOSUPPORT: return "ESOCKTNOSUPPORT";
#if ENOTSUP == EOPNOTSUPP
    case ENOTSUP: return "ENOTSUP / EOPNOTSUPP";
#else
    case ENOTSUP: return "ENOTSUP";
    case EOPNOTSUPP: return "EOPNOTSUPP";
#endif
    case EPFNOSUPPORT: return "EPFNOSUPPORT";
    case EAFNOSUPPORT: return "EAFNOSUPPORT";
    case EADDRINUSE: return "EADDRINUSE";
    case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
    case ENETDOWN: return "ENETDOWN";
    case ENETUNREACH: return "ENETUNREACH";
    case ENETRESET: return "ENETRESET";
    case ECONNABORTED: return "ECONNABORTED";
    case ECONNRESET: return "ECONNRESET";
    case ENOBUFS: return "ENOBUFS";
    case EISCONN: return "EISCONN";
    case ENOTCONN: return "ENOTCONN";
    case ESHUTDOWN: return "ESHUTDOWN";
    case ETOOMANYREFS: return "ETOOMANYREFS";
    case ETIMEDOUT: return "ETIMEDOUT";
    case ECONNREFUSED: return "ECONNREFUSED";
    case EHOSTDOWN: return "EHOSTDOWN";
    case EHOSTUNREACH: return "EHOSTUNREACH";
    case EALREADY: return "EALREADY";
    case EINPROGRESS: return "EINPROGRESS";
    case ESTALE: return "ESTALE";
    case EUCLEAN: return "EUCLEAN";
    case ENOTNAM: return "ENOTNAM";
    case ENAVAIL: return "ENAVAIL";
    case EISNAM: return "EISNAM";
    case EREMOTEIO: return "EREMOTEIO";
    case EDQUOT: return "EDQUOT";
    case ENOMEDIUM: return "ENOMEDIUM";
    case EMEDIUMTYPE: return "EMEDIUMTYPE";
    case ECANCELED: return "ECANCELED";
    case ENOKEY: return "ENOKEY";
    case EKEYEXPIRED: return "EKEYEXPIRED";
    case EKEYREVOKED: return "EKEYREVOKED";
    case EKEYREJECTED: return "EKEYREJECTED";
    case EOWNERDEAD: return "EOWNERDEAD";
    case ENOTRECOVERABLE: return "ENOTRECOVERABLE";
    case ERFKILL: return "ERFKILL";
    case EHWPOISON: return "EHWPOISON";

    default: return NULL;
    }
    // clang-format on
}

static __thread char bdus_error_message_[1024] = { '\0' };

const char *bdus_get_error_message_(void)
{
    return bdus_error_message_;
}

static void bdus_set_error_common_(
    bool append_errno_message, int errno_value,
    const char *error_message_format, va_list error_message_args)
{
    // format error message

    const int result = vsnprintf(
        bdus_error_message_, sizeof(bdus_error_message_), error_message_format,
        error_message_args);

    // append errno description

    if (append_errno_message && result >= 0
        && (size_t)result < sizeof(bdus_error_message_))
    {
        char errno_msg[991] = { '\0' };

        if (strerror_r(errno_value, errno_msg, sizeof(errno_msg)) == 0)
        {
            const char *const symbolic_name =
                bdus_errno_symbolic_name_(errno_value);

            if (symbolic_name)
            {
                snprintf(
                    bdus_error_message_ + result,
                    sizeof(bdus_error_message_) - (size_t)result,
                    " (errno = %s: %s)", symbolic_name, errno_msg);
            }
            else
            {
                // unknown errno value

                snprintf(
                    bdus_error_message_ + result,
                    sizeof(bdus_error_message_) - (size_t)result,
                    " (errno = %d: %s)", errno_value, errno_msg);
            }
        }
    }

    // set errno

    errno = errno_value;
}

void bdus_set_error_(int errno_value, const char *error_message_format, ...)
{
    va_list error_message_args;
    va_start(error_message_args, error_message_format);

    bdus_set_error_common_(
        false, errno_value, error_message_format, error_message_args);

    va_end(error_message_args);
}

void bdus_set_error_append_errno_(
    int errno_value, const char *error_message_format, ...)
{
    va_list error_message_args;
    va_start(error_message_args, error_message_format);

    bdus_set_error_common_(
        true, errno_value, error_message_format, error_message_args);

    va_end(error_message_args);
}

/* -------------------------------------------------------------------------- */
