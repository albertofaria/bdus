/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_CONFIG_H_
#define KBDUS_HEADER_CONFIG_H_

/* -------------------------------------------------------------------------- */

#include <linux/sizes.h>

/* -------------------------------------------------------------------------- */

// (int) The hard maximum number for the `max_devices` module parameter, which
// specifies how many BDUS devices can exist simultaneously.
#define KBDUS_HARD_MAX_DEVICES 4096

// (u32) The default value for `kbdus_config.max_read_write_size`.
#define KBDUS_DEFAULT_MAX_READ_WRITE_SIZE ((u32)SZ_256K)

// (u32) The maximum value for `kbdus_config.max_read_write_size`.
#define KBDUS_HARD_MAX_READ_WRITE_SIZE ((u32)SZ_1M)

// (u32) The maximum value for `kbdus_config.max_outstanding_reqs`.
#define KBDUS_HARD_MAX_OUTSTANDING_REQS 256u

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_CONFIG_H_ */
