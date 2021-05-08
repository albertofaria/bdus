/* SPDX-License-Identifier: MIT */
/* -------------------------------------------------------------------------- */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* -------------------------------------------------------------------------- */

#define TEST_IOCTL_NONE _IO(42, 100)
#define TEST_IOCTL_READ _IOR(42, 101, uint64_t)
#define TEST_IOCTL_WRITE _IOW(42, 102, uint64_t)
#define TEST_IOCTL_READ_WRITE _IOWR(42, 103, uint64_t)

int main(int argc, char **argv)
{
    if (argc != 3)
        return 1;

    const int fd = open(argv[2], O_RDWR | O_DIRECT);

    if (fd < 0)
        return 1;

    bool success;
    uint64_t arg = 1234;

    errno = 0;

    if (strcmp(argv[1], "none") == 0)
        success = ioctl(fd, TEST_IOCTL_NONE) == 0;
    else if (strcmp(argv[1], "read") == 0)
        success = ioctl(fd, TEST_IOCTL_READ, &arg) == 0;
    else if (strcmp(argv[1], "write") == 0)
        success = ioctl(fd, TEST_IOCTL_WRITE, &arg) == 0 && arg == 2345;
    else if (strcmp(argv[1], "read-write") == 0)
        success = ioctl(fd, TEST_IOCTL_READ_WRITE, &arg) == 0 && arg == 2345;
    else
        success = false;

    if (!success)
        fprintf(stderr, "mode %s, ioctl errno: %s\n", argv[1], strerror(errno));

    return success ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
