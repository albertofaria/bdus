/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef KBDUS_HEADER_INVERTER_H_
#define KBDUS_HEADER_INVERTER_H_

/* -------------------------------------------------------------------------- */

#include <kbdus.h>

#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/types.h>

/* -------------------------------------------------------------------------- */

int kbdus_inverter_init(void) __init;
void kbdus_inverter_exit(void);

/* -------------------------------------------------------------------------- */

struct kbdus_inverter;

struct kbdus_inverter_pdu
{
    u64 handle_seqnum;
    u16 handle_index;

    unsigned int ioctl_command;
    void *ioctl_argument;

    int error;
    int error_ioctl;
};

/* -------------------------------------------------------------------------- */

/**
 * CONTEXT: Must be called from process context.
 */
struct kbdus_inverter *
    kbdus_inverter_create(const struct kbdus_device_config *device_config);

/**
 * CONTEXT: Must be called from process context.
 */
void kbdus_inverter_destroy(struct kbdus_inverter *inverter);

/**
 * Must be called at least once before destroying instance. Can be called one or
 * more times.
 *
 * Cause all pending requests to fail with -EIO, and any new pushed requests
 * will also fail immediately with -EIO. Also makes available infinite
 * termination requests.
 *
 * Also causes pending and future ioctls to fail with -EIO.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_terminate(struct kbdus_inverter *inverter);

/**
 * When deactivated, request pullers get infinite termination requests. They can
 * still complete previously gotten requests, and request pushers can still
 * submit requests, but these are kept on hold.
 *
 * When reactivated, pushed requests become again available for pullers.
 *
 * If flush is true, a flush request is sent before the infinite termination
 * requests.
 *
 * Does nothing if the inverter is already inactive.
 *
 * Must *not* be called if inverter was already terminated.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_deactivate(struct kbdus_inverter *inverter, bool flush);

/**
 * If was previously inactive, then any requests that were "awaiting completion"
 * become "awaiting get".
 *
 * Does nothing if the inverter is already active.
 *
 * Must *not* be called if inverter was already terminated.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_activate(struct kbdus_inverter *inverter);

/**
 * May be called 0 or more times. Even after terminating inverter. Has no effect
 * if called after terminating inverter. Never sleeps. Has no effect if a
 * "device available" request is already available.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_submit_device_available_notification(
    struct kbdus_inverter *inverter);

/**
 * The request is later completed by putting a negated errno value in an `int`
 * in the first 4 bytes of the queue request's PDU.
 *
 * Takes a request in the "free" state, initializes it to represent the given
 * request, and puts it in "awaiting get" state. Returns the request handle.
 *
 * Returns 0 (the null handle, meaning no request) if the request is failed by
 * this function because the inverter has been terminated.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * This also calls blk_mq_start_request() on the request.
 *
 * CONTEXT: Don't care.
 *
 * SLEEPING: Never sleeps.
 */
int kbdus_inverter_submit_request(
    struct kbdus_inverter *inverter, struct request *req);

/**
 * Fails the given request due to time out.
 *
 * A negated errno value is put in an `int` in the first 4 bytes of the queue
 * request's PDU.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Don't care.
 *
 * SLEEPING: Never sleeps.
 *
 * Return: A value suitable to be returned by the `timeout` callback of `struct
 *     blk_mq_ops`.
 */
enum blk_eh_timer_return kbdus_inverter_timeout_request(
    struct kbdus_inverter *inverter, struct request *req);

/* -------------------------------------------------------------------------- */

struct kbdus_inverter_item
{
    u64 handle_seqnum;
    struct request *req;
    u16 handle_index;
    u16 type;

#if BITS_PER_LONG == 64
    u8 padding_[4];
#endif
};

/**
 * Blocks until a request is available to be processed, and returns a pointer to
 * that request.
 *
 * Takes a request in the "awaiting get" state and puts it in the "being gotten"
 * state.
 *
 * CONCURRENCY: This function may be invoked concurrently with itself and with
 * any other function exposed by this component, with the exception that this
 * function may *not* be invoked concurrently on the same inverter with
 * `kbdus_inverter_destroy()`.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Might sleep.
 *
 * Returns ERR_PTR(-ERESTARTSYS) if interrupted.
 */
const struct kbdus_inverter_item *
    kbdus_inverter_begin_item_get(struct kbdus_inverter *inverter);

/**
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_commit_item_get(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item);

/**
 * Takes a request in the "being gotten" state and puts it in the "awaiting get"
 * state.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_abort_item_get(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item);

/**
 * Takes a request in the "awaiting completion" state and puts it in the "being
 * completed" state.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 *
 * Returns NULL if the request was already completed (can happen due to timeouts
 * or cancellations).
 *
 * Returns ERR_PTR(-EINVAL) if the request_handle is invalid.
 */
const struct kbdus_inverter_item *kbdus_inverter_begin_item_completion(
    struct kbdus_inverter *inverter, u16 item_handle_index,
    u64 item_handle_seqnum);

/**
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_commit_item_completion(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item,
    int negated_errno);

/**
 * Takes a request in the "being completed" state and puts it in the "awaiting
 * completion" state.
 *
 * CONTEXT: Must be called from process context.
 *
 * SLEEPING: Never sleeps.
 */
void kbdus_inverter_abort_item_completion(
    struct kbdus_inverter *inverter, const struct kbdus_inverter_item *item);

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_INVERTER_H_ */
