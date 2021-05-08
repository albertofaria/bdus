/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_UTILITIES_H_
#define KBDUS_HEADER_UTILITIES_H_

/* -------------------------------------------------------------------------- */

#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/stringify.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/* -------------------------------------------------------------------------- */

// arguments must be of unsigned type
// arguments may be evaluated multiple times
#define kbdus_is_zero_or_power_of_two(x) (((x) & ((x)-1)) == 0)

// argument must be of unsigned type
// argument may be evaluated multiple times
#define kbdus_is_power_of_two(x) (((x) & ((x)-1)) == 0 && (x) != 0)

// arguments must be of unsigned type
// arguments may be evaluated multiple times
#define kbdus_is_zero_or_multiple_of(x, y) (((x) % (y)) == 0)

// arguments must be of unsigned type
// arguments may be evaluated multiple times
#define kbdus_is_positive_multiple_of(x, y) (((x) % (y)) == 0 && (x) != 0)

/* -------------------------------------------------------------------------- */

// This wraps access_ok(), which lost the 'type' argument in Linux 5.0

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define KBDUS_VERIFY_READ
#define KBDUS_VERIFY_WRITE
#define kbdus_access_ok(type, addr, size) access_ok((addr), (size))
#else
#define KBDUS_VERIFY_READ VERIFY_READ
#define KBDUS_VERIFY_WRITE VERIFY_WRITE
#define kbdus_access_ok(type, addr, size) access_ok((type), (addr), (size))
#endif

/**
 * This has linear complexity and is only intended to be used in assertions off
 * the critical path.
 */
static inline int kbdus_list_length(const struct list_head *head)
{
    int len;
    struct list_head *iter;

    len = 0;

    list_for_each(iter, head)
    {
        len += 1;
    }

    return len;
}

/* -------------------------------------------------------------------------- */

#define kbdus_assert(condition)                                                \
    do                                                                         \
    {                                                                          \
        if (unlikely(!(condition)))                                            \
        {                                                                      \
            printk( \
                KERN_ALERT "kbdus: " __FILE__ ":" __stringify(__LINE__) \
                ": assertion failed, system may be in an inconsistent state: " \
                __stringify(condition) "\n" \
                );                                                          \
        }                                                                      \
    } while (0)

#if KBDUS_DEBUG
#define kbdus_assert_if_debug(condition) kbdus_assert(condition)
#else
#define kbdus_assert_if_debug(condition)                                       \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif

#if KBDUS_DEBUG
#define kbdus_log_if_debug(message_format, ...)                                \
    do                                                                         \
    {                                                                          \
        printk(                                                                \
            KERN_DEBUG "kbdus: " __FILE__                                      \
                       ":" __stringify(__LINE__) ": " message_format "\n",     \
            ##__VA_ARGS__);                                                    \
    } while (0)
#else
#define kbdus_log_if_debug(message_format, ...)                                \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_UTILITIES_H_ */
