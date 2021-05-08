/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_TRANSCEIVER_H_
#define KBDUS_HEADER_TRANSCEIVER_H_

/* -------------------------------------------------------------------------- */

#include <kbdus.h>
#include <kbdus/inverter.h>

#include <linux/init.h>
#include <linux/mm_types.h>

/* -------------------------------------------------------------------------- */

int kbdus_transceiver_init(void) __init;
void kbdus_transceiver_exit(void);

/* -------------------------------------------------------------------------- */

struct kbdus_transceiver;

int kbdus_transceiver_validate_and_adjust_config(
    struct kbdus_device_and_fd_config *config);

// The transceiver must not outlive the inverter.
struct kbdus_transceiver *kbdus_transceiver_create(
    const struct kbdus_device_and_fd_config *config,
    struct kbdus_inverter *inverter);

void kbdus_transceiver_destroy(struct kbdus_transceiver *transceiver);

int kbdus_transceiver_handle_control_ioctl(
    struct kbdus_transceiver *transceiver, unsigned int command,
    unsigned long argument);

int kbdus_transceiver_handle_control_mmap(
    struct kbdus_transceiver *transceiver, struct vm_area_struct *vma);

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_TRANSCEIVER_H_ */
