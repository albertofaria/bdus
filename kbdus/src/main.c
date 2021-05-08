/* SPDX-License-Identifier: GPL-2.0-only */
/* -------------------------------------------------------------------------- */

#include <kbdus.h>
#include <kbdus/config.h>
#include <kbdus/control.h>
#include <kbdus/device.h>
#include <kbdus/inverter.h>
#include <kbdus/transceiver.h>
#include <kbdus/utilities.h>

#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/build_bug.h>
#else
#include <linux/bug.h>
#endif

/* -------------------------------------------------------------------------- */

MODULE_LICENSE("GPL");

// clang-format off
MODULE_VERSION(__stringify(
KBDUS_HEADER_VERSION_MAJOR.KBDUS_HEADER_VERSION_MINOR.KBDUS_HEADER_VERSION_PATCH
));
// clang-format on

MODULE_DESCRIPTION(
    "This module is part of BDUS, a framework for developing Block Devices in"
    " User Space (https://github.com/albertofaria/bdus).");

/* -------------------------------------------------------------------------- */

static int __init kbdus_init_(void)
{
    int ret;

    // initialize components

    if ((ret = kbdus_inverter_init()) != 0)
        goto error;

    if ((ret = kbdus_transceiver_init()) != 0)
        goto error_inverter_exit;

    if ((ret = kbdus_device_init()) != 0)
        goto error_transceiver_exit;

    if ((ret = kbdus_control_init()) != 0)
        goto error_device_exit;

    // print success message

    kbdus_log_if_debug("Loaded.");

    return 0;

    // failure

error_device_exit:
    kbdus_device_exit();
error_transceiver_exit:
    kbdus_transceiver_exit();
error_inverter_exit:
    kbdus_inverter_exit();
error:
    return ret;
}

static void __exit kbdus_exit_(void)
{
    // terminate components

    kbdus_control_exit();
    kbdus_device_exit();
    kbdus_transceiver_exit();
    kbdus_inverter_exit();

    // print success message

    kbdus_log_if_debug("Unloaded.");
}

module_init(kbdus_init_);
module_exit(kbdus_exit_);

/* -------------------------------------------------------------------------- */
