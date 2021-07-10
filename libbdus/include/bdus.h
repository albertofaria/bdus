/* SPDX-License-Identifier: MIT */

#ifndef LIBBDUS_HEADER_BDUS_H_
#define LIBBDUS_HEADER_BDUS_H_

/* -------------------------------------------------------------------------- */

#define BDUS_HEADER_VERSION_MAJOR 0
#define BDUS_HEADER_VERSION_MINOR 1
/**
 * The three components of the version of the `bdus.h` header file that was
 * included.
 *
 * Note that this version is determined at compile time and may differ from that
 * returned by `bdus_get_libbdus_version()`, which is only determined at run
 * time.
 */
#define BDUS_HEADER_VERSION_PATCH 1

#if !defined(BDUS_REQUIRE_VERSION_MAJOR)                                       \
    && !defined(BDUS_REQUIRE_VERSION_MINOR)                                    \
    && !defined(BDUS_REQUIRE_VERSION_PATCH)

#define BDUS_REQUIRE_VERSION_MAJOR BDUS_HEADER_VERSION_MAJOR
#define BDUS_REQUIRE_VERSION_MINOR BDUS_HEADER_VERSION_MINOR
/**
 * These three macros can be defined prior to including `bdus.h` to specify what
 * BDUS version is required by the driver. This ensures that, regardless of the
 * BDUS version installed in the system:
 *
 * - The driver fails to compile against a version of BDUS that is not backward
 *   compatible with the required version (in the sense defined in <a
 *   href="versions.html#versioning-scheme">Versioning scheme</a>);
 * - The driver fails to compile if it relies on features not available in the
 *   required version of BDUS;
 * - The compiled driver will be able to run against any version of *libbdus*
 *   that is backward compatible with the required version (in the sense defined
 *   in <a href="versions.html#versioning-scheme">Versioning scheme</a>).
 *
 * If not defined when `bdus.h` is included, these macros are then defined as
 * synonyms to the `BDUS_HEADER_VERSION_{MAJOR,MINOR,PATCH}` macros.
 */
#define BDUS_REQUIRE_VERSION_PATCH BDUS_HEADER_VERSION_PATCH

#elif !defined(BDUS_REQUIRE_VERSION_MAJOR)                                     \
    || !defined(BDUS_REQUIRE_VERSION_MINOR)                                    \
    || !defined(BDUS_REQUIRE_VERSION_PATCH)

#error Must either define all of BDUS_REQUIRE_VERSION_MAJOR, \
BDUS_REQUIRE_VERSION_MINOR, and BDUS_REQUIRE_VERSION_PATCH or none at all

#elif BDUS_REQUIRE_VERSION_MAJOR != 0 || BDUS_REQUIRE_VERSION_MINOR != 1       \
    || BDUS_REQUIRE_VERSION_PATCH > 1

#error bdus.h has version 0.1.1 but incompatible version was required

#error

#endif

/* -------------------------------------------------------------------------- */

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* driver development */

struct bdus_ops;
struct bdus_attrs;

/** \brief Holds context information about a driver and its device. */
struct bdus_ctx
{
    /** \brief The device's numerical identifier. */
    const uint64_t id;

    /** \brief The absolute path to the device's block special file. */
    const char *const path;

    /** \brief Driver callbacks. */
    const struct bdus_ops *const ops;

    /** \brief Device and driver attributes. */
    const struct bdus_attrs *const attrs;

    /** \brief Whether the driver is being run with `bdus_rerun()`. */
    const bool is_rerun;

    /**
     * \brief A pointer to be used freely by the driver's callbacks.
     *
     * The initial value for this field is given by the `private_data` parameter
     * to `bdus_run()` or `bdus_rerun()`. BDUS never inspects nor alters the
     * value of this field.
     */
    void *private_data;

#if BDUS_REQUIRE_VERSION_MAJOR == 0 && BDUS_REQUIRE_VERSION_MINOR == 1         \
    && BDUS_REQUIRE_VERSION_PATCH == 1

    /** \brief The device's major number. */
    const uint32_t major;

    /** \brief The device's minor number. */
    const uint32_t minor;

#endif
};

/**
 * \brief Holds pointers to driver management and request processing callbacks.
 *
 * All callbacks are optional. Request types for which no callback is provided
 * are reported not to be supported by the device.
 *
 * See also <a href="block-devices.html#request-types">Request types</a> and <a
 * href="developing-drivers.html#device-life-cycle">Device life cycle</a>.
 *
 * \note Values of this type should **always** be zero-initialized, as other
 *       fields may be added in future backward-compatible versions of BDUS and
 *       non-public fields may exist.
 */
struct bdus_ops
{
    /**
     * \brief Callback for initializing the driver.
     *
     * If set to `NULL`, this callback defaults to doing nothing.
     *
     * This callback is invoked on a driver before it starts serving any
     * requests (*i.e.*, before any other callback is invoked). It is invoked
     * only once.
     *
     * If this callback fails (by returning a non-zero value), `terminate()` is
     * *not* invoked and `bdus_run()` or `bdus_rerun()` returns.
     *
     * This callback is never run concurrently with any other callback on the
     * same device, and is guaranteed to be invoked before `bdus_run()`
     * daemonizes the current process.
     *
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value or `bdus_abort` should be returned.
     */
    int (*initialize)(struct bdus_ctx *ctx);

    /**
     * \brief Callback to be invoked after the device becomes available to
     *        clients.
     *
     * The block special file for the device is only guaranteed to exist once
     * this callback is invoked.
     *
     * If set to `NULL`, this callback defaults to outputting the device's path,
     * followed by a newline, to `stdout`.
     *
     * Note that device creation may fail, and as such this callback might never
     * be invoked. However, when this callback is invoked, it is guaranteed that
     * `initialize()` was previously run.
     *
     * Also note that it is possible for several request processing callbacks to
     * be run before the device becomes available to clients.
     *
     * If this callback fails (by returning a non-zero value), `terminate()` is
     * immediately invoked and `bdus_run()` or `bdus_rerun()` returns.
     *
     * This callback is never run concurrently with any other callback on the
     * same device, and is guaranteed to be invoked before the current process
     * is daemonized (which occurs if attribute `dont_daemonize` is `false`).
     *
     * When using `bdus_rerun()`, this callback is also invoked even if the
     * existing device was already available to clients.
     *
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value or `bdus_abort` should be returned.
     */
    int (*on_device_available)(struct bdus_ctx *ctx);

    /**
     * \brief Callback for terminating the driver.
     *
     * If set to `NULL`, this callback defaults to doing nothing.
     *
     * This callback is invoked on a driver after it has stopped serving
     * requests. After this callback is invoked, no other callbacks are run. It
     * is invoked only once.
     *
     * When this callback is invoked, it is guaranteed that `initialize()` was
     * previously run. However, if `initialize()` failed, this callback is not
     * invoked.
     *
     * Note that as the driver may be terminated before the device becomes
     * available to users, `on_device_available()` might not have run when this
     * callback is invoked.
     *
     * Note also that the driver may be killed or otherwise exit abnormally, and
     * as such this callback might never be invoked.
     *
     * This callback is never run concurrently with any other callback for the
     * same driver.
     *
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value or `bdus_abort` should be returned.
     */
    int (*terminate)(struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *read* requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p buffer points to a buffer of \p size bytes, aligned in memory to
     *     the system's page size;
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_read_write_size`.
     *
     * \param buffer The buffer to which the data should be read.
     * \param offset The offset (in bytes) into the device at which the read
     *        should take place.
     * \param size The number of bytes that should be read.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*read)(
        char *buffer, uint64_t offset, uint32_t size, struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *write* requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p buffer points to a buffer of \p size bytes, aligned in memory to
     *     the system's page size;
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_read_write_size`.
     *
     * \param buffer A buffer containing the data to be written.
     * \param offset The offset (in bytes) into the device at which the write
     *        should take place.
     * \param size The number of bytes that should be written.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*write)(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *write same* requests.
     *
     * *Write same* requests are used to write the same data to several
     * contiguous logical blocks of the device.
     *
     * If this callback is not implemented but the `write` callback is, *write
     * same* requests will be transparently converted into equivalent *write*
     * requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p buffer points to a buffer of `ctx->attrs->logical_block_size`
     *     bytes, aligned in memory to the system's page size;
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_write_same_size`.
     *
     * \param buffer A buffer containing the data to be written to each logical
     *        block.
     * \param offset The offset (in bytes) into the device at which the write
     *        should take place.
     * \param size The number of bytes that should be written.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*write_same)(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *write zeros* requests.
     *
     * *Write zeros* requests are used to write zeros to a contiguous range of
     * the device.
     *
     * If `may_unmap` is `false`, the driver must ensure that subsequent writes
     * to the same range do not fail due to insufficient space. For example, for
     * a thin-provisioned device, space must be allocated for the zeroed range.
     *
     * If this callback is not implemented but the `write` callback is, *write
     * zeros* requests will be transparently converted into equivalent *write*
     * requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_write_zeros_size`.
     *
     * \param offset The offset (in bytes) into the device at which the write
     *        should take place.
     * \param size The number of bytes that should be set to zero.
     * \param may_unmap Whether subsequent writes to the same range are allowed
     *        to fail due to insufficient space.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*write_zeros)(
        uint64_t offset, uint32_t size, bool may_unmap, struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *FUA write* requests.
     *
     * *FUA write* requests are used to perform a Force Unit Access (FUA) write
     * to the device, meaning that the written data should reach persistent
     * storage before this callback returns. See also <a
     * href="block-devices.html#caches">Caches</a>.
     *
     * If this callback is implemented, then the `flush` callback must also be
     * implemented.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p buffer points to a buffer of \p size bytes, aligned in memory to
     *     the system's page size;
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_read_write_size`.
     *
     * \param buffer A buffer containing the data to be written.
     * \param offset The offset (in bytes) into the device at which the write
     *        should take place.
     * \param size The number of bytes that should be written.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*fua_write)(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *flush* requests.
     *
     * *Flush* requests are used to flush the device's write-back cache. See
     * also <a href="block-devices.html#caches">Caches</a>.
     *
     * If this is callback is not implemented, it is assumed that the device
     * does not feature a write-back cache (implying that upon completion of
     * write requests, data is in permanent storage), and as such does not
     * require flushing.
     *
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*flush)(struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *discard* requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_discard_erase_size`.
     *
     * \param offset The offset (in bytes) into the device at which the region
     *        to be discarded starts.
     * \param size The size (in bytes) of the region to be discarded.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*discard)(uint64_t offset, uint32_t size, struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *secure erase* requests.
     *
     * When BDUS invokes this callback, it is guaranteed that:
     *
     *   - \p offset is a multiple of `ctx->attrs->logical_block_size`;
     *   - \p size is a positive multiple of `ctx->attrs->logical_block_size`;
     *   - `offset + size <= ctx->attrs->size`;
     *   - `size <= ctx->attrs->max_discard_erase_size`.
     *
     * \param offset The offset (in bytes) into the device at which the region
     *        to be erased starts.
     * \param size The size (in bytes) of the region to be erased.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Values other than 0, `ENOLINK`,
     *         `ENOSPC`, `ETIMEDOUT`, and `bdus_abort` are converted to `EIO`.
     */
    int (*secure_erase)(uint64_t offset, uint32_t size, struct bdus_ctx *ctx);

    /**
     * \brief Callback for serving *ioctl* requests.
     *
     * The value of `argument` depends on the *ioctl* command specified by the
     * `command` parameter. Assuming that `dir = _IOC_DIR(command)` and `size =
     * _IOC_SIZE(command)`:
     *
     * - If `dir == _IOC_NONE`, then \p argument is `NULL`;
     * - If `dir == _IOC_READ`, then \p argument points to a buffer of `size`
     *   bytes containing the argument data provided by the client who submitted
     *   the *ioctl* request, and changes to this data are ignored;
     * - If `dir == _IOC_WRITE`, then \p argument points to a zero-filled buffer
     *   of `size` bytes, and changes to this data are propagated to the
     *   argument of the client who submitted the *ioctl* request *if and only
     *   if* this callback returns 0;
     * - If `dir == _IOC_READ | _IOC_WRITE`, then \p argument points to a buffer
     *   of `size` bytes containing the argument data provided by the client who
     *   submitted the *ioctl* request, and changes to this data are propagated
     *   to the argument of the client *if and only if* this callback returns 0.
     *
     * \param command The *ioctl* command.
     * \param argument The argument.
     * \param ctx Information about the device and driver.
     *
     * \return On success, this callback should return 0. On failure, an `errno`
     *         value should be returned to fail the request. Alternatively,
     *         `bdus_abort` can be returned to indicate an unrecoverable driver
     *         error and terminate the driver. Invalid values are converted to
     *         `EIO`.
     */
    int (*ioctl)(uint32_t command, void *argument, struct bdus_ctx *ctx);
};

/**
 * \brief Holds the attributes of a device and driver.
 *
 * \note Values of this type should **always** be zero-initialized, as other
 *       fields may be added in future backward-compatible versions of BDUS and
 *       non-public fields may exist.
 */
struct bdus_attrs
{
    /**
     * \brief The device's logical block size, in bytes.
     *
     * This should be set to the smallest size that the driver is able to
     * address.
     *
     * When using `bdus_run()`, must be a power of two greater than or equal to
     * 512 and less than or equal to the system's page size.
     *
     * When using `bdus_rerun()`, must either be 0 or equal to the existing
     * device's logical block size. (If set to 0, this attribute's value in
     * `ctx->attrs` as available from driver callbacks will be equal to the
     * existing device's logical block size.)
     */
    uint32_t logical_block_size;

    /**
     * \brief The device's physical block size, in bytes.
     *
     * This should be set to the smallest size that the driver can operate on
     * without reverting to read-modify-write operations.
     *
     * When using `bdus_run()`, must either be 0 or a power of two greater than
     * or equal to `logical_block_size` and less than or equal to the system's
     * page size. (If set to 0, this attribute's value in `ctx->attrs` as
     * available from driver callbacks will be equal to `logical_block_size`.)
     *
     * When using `bdus_rerun()`, must either be 0 or equal to the existing
     * device's physical block size. (If set to 0, this attribute's value in
     * `ctx->attrs` as available from driver callbacks will be equal to the
     * existing device's physical block size.)
     */
    uint32_t physical_block_size;

    /**
     * \brief The size of the device, in bytes.
     *
     * When using `bdus_run()`, must be a positive multiple of
     * `physical_block_size`, or of `logical_block_size` if the former is 0.
     *
     * When using `bdus_rerun()`, must either be 0 or equal to the existing
     * device's size. (If set to 0, this attribute's value in `ctx->attrs` as
     * available from driver callbacks will be equal to the existing device's
     * size.)
     */
    uint64_t size;

    /**
     * \brief The maximum number of invocations of the callbacks in `struct
     *        bdus_ops` that may be in progress simultaneously.
     *
     * See also <a href="developing-drivers.html#concurrency">Concurrency</a>.
     *
     * This attribute may take on a different value in `ctx->attrs` as available
     * from driver callbacks, according to the following rules:
     *
     * - If this value is 0, it is set to 1.
     * - Otherwise, this value is either left unmodified or decreased to an
     *   unspecified positive value (but never increased).
     *
     * If this attribute's value (as available in `ctx->attrs` from driver
     * callbacks) is 1, then callbacks are never invoked concurrently, and it is
     * also guaranteed that callbacks are always invoked from the thread that is
     * running `bdus_run()` or `bdus_rerun()` (disregarding daemonization).
     */
    uint32_t max_concurrent_callbacks;

    /**
     * \brief The maximum value for the `size` parameter of the `read`, `write`,
     *        and `fua_write` callbacks in `struct bdus_ops`.
     *
     * When using `bdus_run()`, must either be 0 or greater than or equal to the
     * system's page size.
     *
     * When using `bdus_rerun()`, must either be 0 or greater than or equal to
     * the original driver's value for this attribute.
     *
     * This attribute may take on a different value in `ctx->attrs` as available
     * from driver callbacks, according to the following rules:
     *
     * - When using `bdus_run()`:
     *
     *   - If *none* of the `read`, `write`, and `fua_write` callbacks are
     *     implemented, this value is set to 0;
     *   - Otherwise, if this value is 0, it is set to an unspecified value
     *     greater than or equal to the system's page size;
     *   - Otherwise, this value is either left unmodified or decreased to an
     *     unspecified value greater than or equal to the system's page size
     *     (but never increased).
     *
     * - When using `bdus_rerun()`:
     *
     *   - If *none* of the `read`, `write`, and `fua_write` callbacks are
     *     implemented, this value is set to 0;
     *   - Otherwise, it is set to the original driver's value for this
     *     attribute.
     */
    uint32_t max_read_write_size;

    /**
     * \brief The maximum value for the `size` parameter of the `write_same`
     *        callback in `struct bdus_ops`.
     *
     * When using `bdus_run()`, must either be 0 or greater than or equal to
     * `logical_block_size`.
     *
     * When using `bdus_rerun()`, must either be 0 or greater than or equal to
     * the original driver's value for this attribute.
     *
     * This attribute may take on a different value in `ctx->attrs` as available
     * from driver callbacks, according to the following rules:
     *
     * - When using `bdus_run()`:
     *
     *   - If the `write_same` callback is *not* implemented, this value is set
     *     to 0;
     *   - Otherwise, if this value is 0, it is set to an unspecified value
     *     greater than or equal to `logical_block_size`;
     *   - Otherwise, this value is either left unmodified or decreased to an
     *     unspecified value greater than or equal to `logical_block_size` (but
     *     never increased).
     *
     * - When using `bdus_rerun()`:
     *
     *   - If the `write_same` callback is *not* implemented, this value is set
     *     to 0;
     *   - Otherwise, it is set to the original driver's value for this
     *     attribute.
     */
    uint32_t max_write_same_size;

    /**
     * \brief The maximum value for the `size` parameter of the `write_zeros`
     *        callback in `struct bdus_ops`.
     *
     * When using `bdus_run()`, must either be 0 or greater than or equal to
     * `logical_block_size`.
     *
     * When using `bdus_rerun()`, must either be 0 or greater than or equal to
     * the original driver's value for this attribute.
     *
     * This attribute may take on a different value in `ctx->attrs` as available
     * from driver callbacks, according to the following rules:
     *
     * - When using `bdus_run()`:
     *
     *   - If the `write_zeros` callback is *not* implemented, this value is set
     *     to 0;
     *   - Otherwise, if this value is 0, it is set to an unspecified value
     *     greater than or equal to `logical_block_size`;
     *   - Otherwise, this value is either left unmodified or decreased to an
     *     unspecified value greater than or equal to `logical_block_size` (but
     *     never increased).
     *
     * - When using `bdus_rerun()`:
     *
     *   - If the `write_zeros` callback is *not* implemented, this value is set
     *     to 0;
     *   - Otherwise, it is set to the original driver's value for this
     *     attribute.
     */
    uint32_t max_write_zeros_size;

    /**
     * \brief The maximum value for the `size` parameter of the `discard` and
     *        `secure_erase` callbacks in `struct bdus_ops`.
     *
     * When using `bdus_run()`, must either be 0 or greater than or equal to
     * `logical_block_size`.
     *
     * When using `bdus_rerun()`, must either be 0 or greater than or equal to
     * the original driver's value for this attribute.
     *
     * This attribute may take on a different value in `ctx->attrs` as available
     * from driver callbacks, according to the following rules:
     *
     * - When using `bdus_run()`:
     *
     *   - If both the `discard` and `secure_erase` callbacks are *not*
     *     implemented, this value is set to 0;
     *   - Otherwise, if this value is 0, it is set to an unspecified value
     *     greater than or equal to `logical_block_size`;
     *   - Otherwise, this value is either left unmodified or decreased to an
     *     unspecified value greater than or equal to
     *     `logical_block_size` (but never increased).
     *
     * - When using `bdus_rerun()`:
     *
     *   - If both the `discard` and `secure_erase` callbacks are *not*
     *     implemented, this value is set to 0;
     *   - Otherwise, it is set to the original driver's value for this
     *     attribute.
     */
    uint32_t max_discard_erase_size;

    /**
     * \brief Whether to disable partition scanning for the device.
     *
     * If `true`, the kernel will never attempt to recognize partitions in the
     * device. This is useful to ensure that data in unpartitioned devices is
     * not mistakenly interpreted as partitioning information. See also <a
     * href="block-devices.html#partitions">Partitions</a>.
     *
     * When using `bdus_rerun()`, this attribute is ignored and its value in
     * `ctx->attrs` as available from driver callbacks will be the value given
     * by the device's original driver.
     */
    bool disable_partition_scanning;

    /**
     * \brief Whether *not* to destroy the device in case of driver failure.
     *
     * If this is `false` and the driver terminates abnormally, the
     * corresponding device is automatically destroyed. On the other hand, if
     * this is `true`, the device continues to exist without a controlling
     * driver, allowing another driver to take control of the device by using
     * `bdus_rerun()`. See also <a
     * href="developing-drivers.html#recovering-failed-drivers">Recovering
     * failed drivers</a>.
     *
     * Note that regardless of the value of this attribute, `bdus_rerun()` can
     * be used to replace the driver for a device that already has a controlling
     * driver.
     *
     * When using `bdus_rerun()`, this attribute is ignored and its value in
     * `ctx->attrs` as available from driver callbacks will be the value given
     * by the device's original driver.
     */
    bool recoverable;

    /**
     * \brief Whether *not* to daemonize the process calling `bdus_run()` after
     *        the device becomes available.
     *
     * If this is `false`, then the process that invokes `bdus_run()` or
     * `bdus_rerun()` will be daemonized after the `on_device_available()`
     * callback (or its default implementation) is invoked and completes.
     *
     * After daemonization, `stdin`, `stdout`, and `stderr` are redirected to
     * `/dev/null`. Note, however, that the current working directory and umask
     * are not changed.
     */
    bool dont_daemonize;

    /**
     * \brief Whether to log attribute modifications and callback invocations.
     *
     * If `true`, messages will be printed to `stderr` on device creation
     * showing how attribute were modified, and immediately before every
     * callback invocation showing with what arguments they are being called.
     *
     * Note that messages printed after the driver is daemonized are not
     * visible, since `stderr` is redirected to `/dev/null`.
     */
    bool log;
};

enum
{
    /**
     * \brief Callback return value indicating that the driver should be
     *        aborted.
     *
     * This value can be returned from a `struct bdus_ops` callback to indicate
     * an unrecoverable internal driver error, causing driver execution to
     * terminate and `bdus_run()` or `bdus_rerun()` to return.
     */
    bdus_abort = INT_MIN
};

bool bdus_run_0_1_0_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data);

bool bdus_run_0_1_1_(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data);

/**
 * \brief Runs a driver for a new block device with the specified callbacks and
 *        attributes.
 *
 * This function fails if the effective user ID of the calling process does not
 * correspond to the `root` user. However, the effective user ID may be modified
 * before this function returns (*e.g.*, in the `ops->initialize()` callback).
 *
 * Concurrent invocations of this function with itself and with `bdus_rerun()`
 * are allowed only if `attrs->dont_daemonize` is `true` for all of them.
 *
 * \param ops Driver callbacks.
 * \param attrs Device and driver attributes.
 * \param private_data The initial value for the `private_data` field of the
 *        `struct bdus_ctx` given to the driver's callbacks.
 *
 * \return On success, blocks until the driver is terminated and returns `true`.
 *         On failure, `false` is returned, `errno` is set to an appropriate
 *         error number, and the current error message is set to a string
 *         descriptive of the error (see `bdus_get_error_message()`).
 */
static inline bool bdus_run(
    const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
#if BDUS_REQUIRE_VERSION_MAJOR == 0 && BDUS_REQUIRE_VERSION_MINOR == 1         \
    && BDUS_REQUIRE_VERSION_PATCH == 1
    return bdus_run_0_1_1_(ops, attrs, private_data);
#else
    return bdus_run_0_1_0_(ops, attrs, private_data);
#endif
}

bool bdus_rerun_0_1_0_(
    uint64_t dev_id, const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data);

bool bdus_rerun_0_1_1_(
    uint64_t dev_id, const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data);

/**
 * \brief Runs a driver for an *existing* block device with the specified
 *        callbacks and attributes.
 *
 * See also <a
 * href="developing-drivers.html#replacing-running-drivers">Replacing running
 * drivers</a> and <a
 * href="developing-drivers.html#recovering-failed-drivers">Recovering failed
 * drivers</a>.
 *
 * The existing device may or may not already have a controlling driver. If it
 * does, the controlling driver is sent a flush request and then terminated
 * before the new driver is initialized.
 *
 * If the existing driver terminates abnormally (*e.g.*, crashes or its
 * `terminate()` callback returns an error) and its `recoverable` attribute is
 * `false`, this function fails and the device is subsequently destroyed.
 * However, if its `recoverable` attribute is `true`, the new driver is
 * initialized regardless of whether the existing driver terminates
 * successfully.
 *
 * The new driver must support the same request types as the existing device's
 * original driver. Callbacks for serving other request types may also be
 * provided, but are not used.
 *
 * This function fails if the effective user ID of the calling process does not
 * correspond to the `root` user. However, the effective user ID may be modified
 * before this function returns (*e.g.*, in the `ops->initialize()` callback).
 *
 * Concurrent invocations of this function with itself and with `bdus_run()` are
 * allowed only if `attrs->dont_daemonize` is `true` for all of them.
 *
 * \param dev_id The numerical identifier of the device.
 * \param ops Driver callbacks.
 * \param attrs Device and driver attributes.
 * \param private_data The initial value for the `private_data` field of the
 *        `struct bdus_ctx` given to the driver's callbacks.
 *
 * \return On success, blocks until the driver is terminated and returns `true`.
 *         On failure, `false` is returned, `errno` is set to an appropriate
 *         error number, and the current error message is set to a string
 *         descriptive of the error (see `bdus_get_error_message()`).
 */
static inline bool bdus_rerun(
    uint64_t dev_id, const struct bdus_ops *ops, const struct bdus_attrs *attrs,
    void *private_data)
{
#if BDUS_REQUIRE_VERSION_MAJOR == 0 && BDUS_REQUIRE_VERSION_MINOR == 1         \
    && BDUS_REQUIRE_VERSION_PATCH == 1
    return bdus_rerun_0_1_1_(dev_id, ops, attrs, private_data);
#else
    return bdus_rerun_0_1_0_(dev_id, ops, attrs, private_data);
#endif
}

/* -------------------------------------------------------------------------- */
/* device management */

bool bdus_get_dev_id_from_path_0_1_0_(
    uint64_t *out_dev_id, const char *dev_path);

/**
 * \brief Obtains the numerical identifier of the device referred to by the
 *        given path.
 *
 * The path must point to a block special file, which in turn must reference a
 * device created by BDUS.
 *
 * If this function fails, the value of `*out_dev_id` is not changed.
 *
 * \param out_dev_id Pointer to the value to be modified.
 * \param dev_path A path to a block special file referring to the device.
 *
 * \return On success, returns `true`. On failure, `false` is returned, `errno`
 *         is set to an appropriate error number, and the current error message
 *         is set to a string descriptive of the error (see
 *         `bdus_get_error_message()`).
 */
static inline bool
    bdus_get_dev_id_from_path(uint64_t *out_dev_id, const char *dev_path)
{
    return bdus_get_dev_id_from_path_0_1_0_(out_dev_id, dev_path);
}

bool bdus_flush_dev_0_1_0_(uint64_t dev_id);

/**
 * \brief Flushes a device.
 *
 * This function blocks until all data previously written to the device is
 * persistently stored (or until an error occurs). This has the same effect as
 * performing an `fsync()` or `fdatasync()` call on the device, or executing the
 * `sync` command with a path to the block device as an argument.
 *
 * This function fails if the effective user ID of the calling process does not
 * correspond to the `root` user.
 *
 * \param dev_id The numerical identifier of the device to be flushed.
 *
 * \return On success, returns `true`. On failure, `false` is returned, `errno`
 *         is set to an appropriate error number, and the current error message
 *         is set to a string descriptive of the error (see
 *         `bdus_get_error_message()`).
 */
static inline bool bdus_flush_dev(uint64_t dev_id)
{
    return bdus_flush_dev_0_1_0_(dev_id);
}

bool bdus_destroy_dev_0_1_0_(uint64_t dev_id);

/**
 * \brief Destroys a device.
 *
 * This function prompts the device's driver to terminate (if the device has a
 * controlling driver) and then destroys the device. It blocks until the device
 * is destroyed (or until an error occurs). Note that the driver might be
 * terminated and the device destroyed even if this function fails.
 *
 * This function fails if the effective user ID of the calling process does not
 * correspond to the `root` user.
 *
 * \param dev_id The numerical identifier of the device to be destroyed.
 *
 * \return On success, returns `true`. On failure, `false` is returned, `errno`
 *         is set to an appropriate error number, and the current error message
 *         is set to a string descriptive of the error (see
 *         `bdus_get_error_message()`).
 */
static inline bool bdus_destroy_dev(uint64_t dev_id)
{
    return bdus_destroy_dev_0_1_0_(dev_id);
}

/* -------------------------------------------------------------------------- */
/* errors */

const char *bdus_get_error_message_0_1_0_(void);

/**
 * \brief Returns the current error message of the calling thread.
 *
 * Every function exported by the BDUS user-space library (*libbdus*) sets this
 * string to a descriptive message when it fails. The current error message is
 * not changed under any other circumstance.
 *
 * Each thread has its own current error message, which is an empty string if no
 * error has occurred in the thread.
 *
 * Note that messages returned by this function may differ between releases,
 * even if they are backward compatible.
 *
 * \return A string descriptive of the last error that occurred in the current
 *         thread.
 */
static inline const char *bdus_get_error_message(void)
{
    return bdus_get_error_message_0_1_0_();
}

/* -------------------------------------------------------------------------- */
/* versions */

/** \brief Represents a version number. */
struct bdus_version
{
    /** \brief The *major* version. */
    uint32_t major;

    /** \brief The *minor* version. */
    uint32_t minor;

    /** \brief The *patch* version. */
    uint32_t patch;
};

const struct bdus_version *bdus_get_libbdus_version_0_1_0_(void);

/**
 * \brief Returns the version of *libbdus* against which the calling program is
 *        running.
 *
 * \return The version of *libbdus* against which the calling program is
 *         running.
 */
static inline const struct bdus_version *bdus_get_libbdus_version(void)
{
    return bdus_get_libbdus_version_0_1_0_();
}

bool bdus_get_kbdus_version_0_1_0_(struct bdus_version *out_kbdus_version);

/**
 * \brief Retrieves the version of *kbdus* that is currently installed.
 *
 * This function fails if the effective user ID of the calling process does not
 * correspond to the `root` user.
 *
 * \param out_kbdus_version Pointer to a variable in which kbdus' version should
 *        be stored.
 *
 * \return On success, the version of *kbdus* that is currently installed is
 *         written to \p out_kbdus_version and `true` is returned. On failure,
 *         `false` is returned, `errno` is set to an appropriate error number,
 *         and the current error message is set to a string descriptive of the
 *         error (see `bdus_get_error_message()`).
 */
static inline bool
    bdus_get_kbdus_version(struct bdus_version *out_kbdus_version)
{
    return bdus_get_kbdus_version_0_1_0_(out_kbdus_version);
}

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------- */

#endif /* LIBBDUS_HEADER_BDUS_H_ */
