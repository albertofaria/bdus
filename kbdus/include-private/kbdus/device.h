/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_DEVICE_H_
#define KBDUS_HEADER_DEVICE_H_

/* -------------------------------------------------------------------------- */

#include <kbdus.h>

#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/version.h>

/* -------------------------------------------------------------------------- */

int kbdus_device_init(void) __init;
void kbdus_device_exit(void);

/* -------------------------------------------------------------------------- */

/** \brief Represents a BDUS block device. */
struct kbdus_device;

enum kbdus_device_state
{
    /**
     * Initial state.
     *
     * Can transition by itself to state ACTIVE.
     * Can transition to state TERMINATED by calling kbdus_device_terminate().
     */
    KBDUS_DEVICE_STATE_UNAVAILABLE,

    /**
     * Normal state.
     *
     * Can transition to state INACTIVE by calling kbdus_device_deactivate().
     * Can transition to state TERMINATED by calling kbdus_device_terminate().
     */
    KBDUS_DEVICE_STATE_ACTIVE,

    /**
     * Accepts requests but they are not sent to transceivers, which only
     * receive termination requests.
     *
     * Can transition to state ACTIVE by calling kbdus_device_activate().
     * Can transition to state TERMINATED by calling kbdus_device_terminate().
     */
    KBDUS_DEVICE_STATE_INACTIVE,

    /**
     * Accepts requests but they are immediately failed. When transitioning to
     * this state, existing requests are also immediately failed. Transceivers
     * only receive termination requests.
     *
     * Can not transition to any other state.
     */
    KBDUS_DEVICE_STATE_TERMINATED,
};

u32 kbdus_device_get_major(void);

/**
 * \brief Validates the given device configuration and, if it is valid, adjusts
 *        its fields.
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that
 * concurrent invocations of this function on the same configuration result in
 * undefined behavior.
 *
 * SLEEPING: This function never sleeps.
 */
int kbdus_device_validate_and_adjust_config(struct kbdus_device_config *config);

/**
 * \brief Creates a device.
 *
 * The given configuration must have been previously processed by
 * `kbdus_device_validate_and_adjust_config()`.
 *
 * Returns an `ERR_PTR()` on error.
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component.
 *
 * SLEEPING: This function might sleep.
 */
struct kbdus_device *kbdus_device_create(
    const struct kbdus_device_config *config, int first_minor);

/**
 * \brief Destroys a device.
 *
 * This function does not require interaction with the user-space driver, and
 * may be invoked even if `kbdus_device_terminate()` was not.
 *
 * Note that this function causes all pending requests submitted to the device
 * to fail and does not attempt to persist previously written data.
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same device with
 * **itself**, `kbdus_device_terminate()`,
 * `kbdus_device_handle_control_read_iter()`,
 * `kbdus_device_handle_control_write_iter()`, or
 * `kbdus_device_handle_control_ioctl()`.
 *
 * SLEEPING: This function might sleep.
 */
void kbdus_device_destroy(struct kbdus_device *device);

enum kbdus_device_state
    kbdus_device_get_state(const struct kbdus_device *device);

const struct kbdus_device_config *
    kbdus_device_get_config(const struct kbdus_device *device);

bool kbdus_device_is_read_only(const struct kbdus_device *device);

/**
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same device with
 * `kbdus_device_destroy()`.
 *
 * SLEEPING: This function never sleeps.
 */
dev_t kbdus_device_get_dev(const struct kbdus_device *device);

/**
 * The inverter is owned by the device and is destroyed when the device is
 * destroyed.
 */
struct kbdus_inverter *
    kbdus_device_get_inverter(const struct kbdus_device *device);

/**
 * \brief Request the termination of the given device.
 *
 * This function prevents the submission of new requests to the device and fails
 * already submitted requests, and instructs the driver to terminate.
 *
 * This function may be called more than once on the same device, or never at
 * all. Only the first invocation has any effect.
 *
 * Note that this function may return before the aforementioned process is fully
 * performed.
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same device with
 * `kbdus_device_destroy()`, `kbdus_device_deactivate()`, or
 * `kbdus_device_activate()`.
 *
 * SLEEPING: This function might sleep.
 */
void kbdus_device_terminate(struct kbdus_device *device);

/**
 * If state is:
 *
 * - STARTING, then device is terminated;
 * - ACTIVE, then device becomes INACTIVE and requests given to a transceivers
 *   but not yet completed will later be available again to a future
 *   transceiver;
 * - INACTIVE, then nothing is done;
 * - TERMINATED, then nothing is done.
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same device with
 * `kbdus_device_destroy()`, `kbdus_device_terminate()`, or
 * `kbdus_device_activate()`.
 *
 * SLEEPING: This function never sleeps.
 */
void kbdus_device_deactivate(struct kbdus_device *device, bool flush);

/**
 * MAY ONLY BE CALLED IF THE CURRENT STATE IS "INACTIVE".
 *
 * THREAD-SAFETY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same device with
 * `kbdus_device_destroy()`, `kbdus_device_terminate()`, or
 * `kbdus_device_deactivate()`.
 *
 * SLEEPING: This function never sleeps.
 */
void kbdus_device_activate(struct kbdus_device *device);

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_DEVICE_H_ */
