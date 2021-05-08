/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_CONTROL_H_
#define KBDUS_HEADER_CONTROL_H_

/* -------------------------------------------------------------------------- */

#include <linux/init.h>

/* -------------------------------------------------------------------------- */

int kbdus_control_init(void) __init;
void kbdus_control_exit(void);

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_CONTROL_H_ */
