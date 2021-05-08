/* SPDX-License-Identifier: MIT */

#ifndef KBDUS_HEADER_KBDUS_H_
#define KBDUS_HEADER_KBDUS_H_

/* -------------------------------------------------------------------------- */

#define KBDUS_HEADER_VERSION_MAJOR 0
#define KBDUS_HEADER_VERSION_MINOR 1
/**
 * The three components of the version of the `kbdus.h` header file that was
 * included.
 */
#define KBDUS_HEADER_VERSION_PATCH 0

/* -------------------------------------------------------------------------- */

#if __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

/** \brief A version number. */
struct kbdus_version
{
    /** \brief The *major* version. */
    uint32_t major;

    /** \brief The *minor* version. */
    uint32_t minor;

    /** \brief The *patch* version. */
    uint32_t patch;

    /** \cond PRIVATE */
    uint32_t padding_;
    /** \endcond */
};

/**
 * \brief Configuration for a device.
 *
 * The documentation for each field specifies its directionality, *i.e.*,
 * whether it is read by kbdus (IN), written by kbdus (OUT), or both (IN/OUT),
 * when the configuration is part of the argument for ioctl command
 * `KBDUS_IOCTL_CREATE_DEVICE`.
 *
 * When the configuration is instead part of the argument for ioctl command
 * `KBDUS_IOCTL_ATTACH_TO_DEVICE`, then field `id` has directionality IN and all
 * other fields have directionality OUT.
 *
 * Note that the restrictions documented for each field apply only if it is
 * under a directionality of IN or IN/OUT. Similarly, the documentation for how
 * each field is modified applies only if they are under a directionality of
 * IN/OUT.
 */
struct kbdus_device_config
{
    /**
     * \brief The device's numerical identifier, unique for every device since
     *        module load.
     *
     * Directionality: OUT on create.
     */
    uint64_t id;

    /**
     * \brief The size of the device, in bytes.
     *
     * Directionality: IN on create.
     *
     * Restrictions: Must be a positive multiple of `physical_block_size`, or of
     * `logical_block_size` if the former is 0.
     */
    uint64_t size;

    /**
     * \brief The device's logical block size, in bytes.
     *
     * Directionality: IN on create.
     *
     * Restrictions: Must be a power of two greater than or equal to 512 and
     * less than or equal to the system's page size.
     */
    uint32_t logical_block_size;

    /**
     * \brief The device's physical block size, in bytes.
     *
     * Directionality: IN/OUT on create.
     *
     * Restrictions: Must be 0 or a power of two greater than or equal to
     * `logical_block_size` and less than or equal to the system's page size.
     *
     * How this value is modified:
     *
     * - If this value is 0, it is set to the value of `logical_block_size`.
     * - Otherwise, this value is left unchanged.
     */
    uint32_t physical_block_size;

    /**
     * \brief The maximum size for *read*, *write*, and *FUA write* requests, in
     *        bytes.
     *
     * Note that this does not impose a limit on operations performed by clients
     * of the device, but ensures that requests violating this limit are split
     * into requests that satisfy it.
     *
     * Directionality: IN/OUT on create.
     *
     * Requirements: Must be 0 or a value greater than or equal to the system's
     * page size.
     *
     * How this value is modified:
     *
     * - If `supports_read`, `supports_write`, and `supports_fua_write` are all
     *   false, this value is set to 0;
     * - Otherwise, if this value is 0, it is set to an unspecified positive
     *   multiple of `logical_block_size` that is greater than or equal to the
     *   system's page size;
     * - Otherwise, this value is either left unchanged or decreased to an
     *   unspecified positive multiple of `logical_block_size` that is greater
     *   than or equal to the system's page size (but never increased).
     */
    uint32_t max_read_write_size;

    /**
     * \brief The maximum size for *write same* requests, in bytes.
     *
     * Note that this does not impose a limit on operations performed by clients
     * of the device, but ensures that requests violating this limit are split
     * into requests that satisfy it.
     *
     * Directionality: IN/OUT on create.
     *
     * Requirements: Must be 0 or a value greater than or equal to
     * `logical_block_size`.
     *
     * How this value is modified:
     *
     * - If `supports_write_same` is false, this value is set to 0;
     * - Otherwise, if this value is 0, it is set to an unspecified positive
     *   multiple of `logical_block_size`;
     * - Otherwise, this value is either left unchanged or decreased to an
     *   unspecified positive multiple of `logical_block_size` (but never
     *   increased).
     */
    uint32_t max_write_same_size;

    /**
     * \brief The maximum size for *write zeros* requests, in bytes.
     *
     * Note that this does not impose a limit on operations performed by clients
     * of the device, but ensures that requests violating this limit are split
     * into requests that satisfy it.
     *
     * Directionality: IN/OUT on create.
     *
     * Requirements: Must be 0 or a value greater than or equal to
     * `logical_block_size`.
     *
     * How this value is modified:
     *
     * - If `supports_write_zeros` is false, this value is set to 0;
     * - Otherwise, if this value is 0, it is set to an unspecified positive
     *   multiple of `logical_block_size`;
     * - Otherwise, this value is either left unchanged or decreased to an
     *   unspecified positive multiple of `logical_block_size` (but never
     *   increased).
     */
    uint32_t max_write_zeros_size;

    /**
     * \brief The maximum size for *discard* and *secure erase* requests, in
     *        bytes.
     *
     * Note that this does not impose a limit on operations performed by clients
     * of the device, but ensures that requests violating this limit are split
     * into requests that satisfy it.
     *
     * Directionality: IN/OUT on create.
     *
     * Requirements: Must be 0 or a value greater than or equal to
     * `logical_block_size`.
     *
     * How this value is modified:
     *
     * - If `supports_discard` and `supports_secure_erase` are both false, this
     *   value is set to 0;
     * - Otherwise, if this value is 0, it is set to an unspecified positive
     *   multiple of `logical_block_size`;
     * - Otherwise, this value is either left unchanged or decreased to an
     *   unspecified positive multiple of `logical_block_size` (but never
     *   increased).
     */
    uint32_t max_discard_erase_size;

    /**
     * \brief The maximum number of simultaneously outstanding requests.
     *
     * An outstanding request is one that has been received through a file
     * description but not yet replied to.
     *
     * Directionality: IN/OUT on create.
     *
     * Requirements: Must be positive.
     *
     * How this value is modified: This value is either left unchanged or
     * decreased to an unspecified positive value (but never increased).
     */
    uint32_t max_outstanding_reqs;

    /**
     * \brief Whether the device supports *read* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_read;

    /**
     * \brief Whether the device supports *write* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_write;

    /**
     * \brief Whether the device supports *write same* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_write_same;

    /**
     * \brief Whether the device supports *write zeros* requests (both allowing
     * and disallowing unmapping).
     *
     * Directionality: IN on create.
     */
    uint8_t supports_write_zeros;

    /**
     * \brief Whether the device supports *FUA write* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_fua_write;

    /**
     * \brief Whether the device supports *flush* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_flush;

    /**
     * \brief Whether the device supports *discard* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_discard;

    /**
     * \brief Whether the device supports *secure erase* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_secure_erase;

    /**
     * \brief Whether the device supports *ioctl* requests.
     *
     * Directionality: IN on create.
     */
    uint8_t supports_ioctl;

    /**
     * \brief Whether to mark the device as being rotational.
     *
     * This may have an effect on the behavior of the scheduler for the device,
     * and thus its performance.
     *
     * Directionality: IN on create.
     */
    uint8_t rotational;

    /**
     * \brief Whether request merging should occur.
     *
     * Directionality: IN on create.
     */
    uint8_t merge_requests;

    /**
     * \brief Whether to enable partition scanning for the device.
     *
     * Directionality: IN on create.
     */
    uint8_t enable_partition_scanning;

    /**
     * \brief Whether the device is *recoverable*.
     *
     * If a non-recoverable device is left without an attached file description,
     * it is automatically destroyed. A recoverable device, on the other hand,
     * is not (unless it is left without an attached file description before
     * becoming available to clients), and can only be destroyed using the
     * `KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION` ioctl command.
     *
     * Note that `KBDUS_IOCTL_ATTACH_TO_DEVICE` can be used on a device
     * regardless of whether it is recoverable.
     *
     * Directionality: IN on create.
     */
    uint8_t recoverable;

    /** \cond PRIVATE */
    uint8_t reserved_[71];
    /** \endcond */
};

/**
 * \brief Configuration for a file description.
 *
 * The documentation for each field specifies its directionality, *i.e.*,
 * whether it is read by kbdus but not written (IN), or both read and written
 * (OUT), *regardless* of whether the configuration is part of the argument for
 * ioctl command `KBDUS_IOCTL_CREATE_DEVICE` ("on create") or
 * `KBDUS_IOCTL_ATTACH_TO_DEVICE` ("on attach").
 */
struct kbdus_fd_config
{
    /**
     * \brief How many user-mappable request payload buffers to allocate.
     *
     * Directionality: IN/OUT.
     *
     * How this value is modified: If it is greater than the *adjusted* value of
     * max_outstanding_reqs, then it is set to that value.
     */
    uint32_t num_preallocated_buffers;

    /** \cond PRIVATE */
    uint8_t reserved_[124];
    /** \endcond */
};

/** \brief Configuration for both a device and a file description. */
struct kbdus_device_and_fd_config
{
    /** \brief Configuration for the device. */
    struct kbdus_device_config device;

    /** \brief Configuration for the file description. */
    struct kbdus_fd_config fd;
};

/** \brief Item types. */
enum kbdus_item_type
{
    /**
     * \brief Indicates that the device has become available to clients.
     *
     * Items of this type are not requests and should not be replied to.
     */
    KBDUS_ITEM_TYPE_DEVICE_AVAILABLE,

    /**
     * \brief Indicates that the file description should be closed.
     *
     * Items of this type are not requests and should not be replied to.
     */
    KBDUS_ITEM_TYPE_TERMINATE,

    /**
     * \brief Indicates that the file description should be closed right after
     *        performing a *flush* request.
     *
     * Items of this type are not requests and should not be replied to.
     */
    KBDUS_ITEM_TYPE_FLUSH_AND_TERMINATE,

    /**
     * \brief *Read* request.
     *
     * - 64-bit argument: read offset;
     * - 32-bit argument: number of bytes to be read;
     * - Request payload: (none);
     * - Reply payload: the read data, if the operation was successful;
     * - Reply payload size: the 32-bit argument.
     */
    KBDUS_ITEM_TYPE_READ,

    /**
     * \brief *Write* request.
     *
     * - 64-bit argument: write offset;
     * - 32-bit argument: number of bytes to be written;
     * - Request payload: the data to be written;
     * - Request payload size: the 32-bit argument;
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_WRITE,

    /**
     * \brief *Write same* request.
     *
     * - 64-bit argument: write offset;
     * - 32-bit argument: number of bytes to be written;
     * - Request payload: the data to be written;
     * - Request payload size: the device's logical block size;
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_WRITE_SAME,

    /**
     * \brief *Write zeros* request that *must not* deallocate space.
     *
     * - 64-bit argument: write offset;
     * - 32-bit argument: number of bytes to be zeroed;
     * - Request payload: (none);
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_WRITE_ZEROS_NO_UNMAP,

    /**
     * \brief *Write zeros* request that *may* deallocate space.
     *
     * - 64-bit argument: write offset;
     * - 32-bit argument: number of bytes to be zeroed;
     * - Request payload: (none);
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_WRITE_ZEROS_MAY_UNMAP,

    /**
     * \brief *FUA write* request.
     *
     * - 64-bit argument: write offset;
     * - 32-bit argument: number of bytes to be written;
     * - Request payload: the data to be written;
     * - Request payload size: the 32-bit argument;
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_FUA_WRITE,

    /**
     * \brief *Flush* request.
     *
     * - 64-bit argument: (unused);
     * - 32-bit argument: (unused);
     * - Request payload: (none);
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_FLUSH,

    /**
     * \brief *Discard* request.
     *
     * - 64-bit argument: discard offset;
     * - 32-bit argument: number of bytes to be discarded;
     * - Request payload: (none);
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_DISCARD,

    /**
     * \brief *Secure erase* request.
     *
     * - 64-bit argument: secure erase offset;
     * - 32-bit argument: number of bytes to be securely erased;
     * - Request payload: (none);
     * - Reply payload: (none).
     */
    KBDUS_ITEM_TYPE_SECURE_ERASE,

    /**
     * \brief *ioctl* request.
     *
     * - 64-bit argument: (unused);
     * - 32-bit argument: ioctl command;
     * - Request payload: the argument data, if the ioctl command's direction
     *   has the _IOC_READ bit set;
     * - Request payload size: given by the ioctl command, if the ioctl
     *   command's direction has the _IOC_READ bit set;
     * - Reply payload: the argument data, if the ioctl command's direction has
     *   the _IOC_WRITE bit set;
     * - Reply payload size: given by the ioctl command, if the ioctl command's
     *   direction has the _IOC_WRITE bit set.
     */
    KBDUS_ITEM_TYPE_IOCTL,
};

/** \brief An item. */
struct kbdus_item
{
    /**
     * \brief Pointer to process memory or index of preallocated buffer.
     *
     * The meaning of this field depends on use_preallocated_buffer.
     */
    uint64_t user_ptr_or_buffer_index;

    /** \brief The *seqnum* portion of the handle that identifies this item. */
    uint64_t handle_seqnum;

    /** \brief The *index* portion of the handle that identifies this item. */
    uint16_t handle_index;

    /**
     * \brief Whether the payload for this item (if any) should be stored in a
     *        preallocated buffer or in a region of process memory.
     */
    uint8_t use_preallocated_buffer;

    /** \brief The type of this item. */
    uint8_t type;

    /** \brief The 32-bit argument for this item (if applicable). */
    uint32_t arg32;

    /** \brief The 64-bit argument for this item (if applicable). */
    uint64_t arg64;

    /** \cond PRIVATE */
    uint8_t padding_[32];
    /** \endcond */
};

/** \brief A reply to a request. */
struct kbdus_reply
{
    /**
     * \brief Pointer to process memory or index of preallocated buffer.
     *
     * The meaning of this field depends on use_preallocated_buffer.
     */
    uint64_t user_ptr_or_buffer_index;

    /**
     * \brief The *seqnum* portion of the handle that identifies the item to
     *        which this reply pertains.
     */
    uint64_t handle_seqnum;

    /**
     * \brief The *index* portion of the handle that identifies the item to
     *        which this reply pertains.
     */
    uint16_t handle_index;

    /**
     * \brief Whether the payload for this reply (if any) should be stored in a
     *        preallocated buffer or in a region of process memory.
     */
    uint8_t use_preallocated_buffer;

    /** \cond PRIVATE */
    uint8_t padding1_[1];
    /** \endcond */

    /**
     * \brief 0 if the operation succeeded, errno value otherwise.
     *
     * For non-ioctl requests, values other than 0, ENOLINK, ENOSPC, and
     * ETIMEDOUT are converted into EIO.
     *
     * For ioctl requests, negative values and values greater than 133 are
     * converted into EIO.
     */
    int32_t error;

    /** \cond PRIVATE */
    uint8_t padding2_[40];
    /** \endcond */
};

/** \brief The common prefix of `struct kbdus_reply` and `struct kbdus_item`. */
struct kbdus_reply_or_item_common
{
    /**
     * \brief An alias for the `kbdus_reply.user_ptr_or_buffer_index` and
     *        `kbdus_item.user_ptr_or_buffer_index` fields.
     */
    uint64_t user_ptr_or_buffer_index;

    /**
     * \brief An alias for the `kbdus_reply.handle_seqnum` and
     *        `kbdus_item.handle_seqnum` fields.
     */
    uint64_t handle_seqnum;

    /**
     * \brief An alias for the `kbdus_reply.handle_index` and
     *        `kbdus_item.handle_index` fields.
     */
    uint16_t handle_index;

    /**
     * \brief An alias for the `kbdus_reply.use_preallocated_buffer` and
     *        `kbdus_item.use_preallocated_buffer` fields.
     */
    uint8_t use_preallocated_buffer;
};

/** \brief A reply *or* an item. */
union kbdus_reply_or_item
{
    /** \brief The reply. */
    struct kbdus_reply reply;

    /** \brief The item. */
    struct kbdus_item item;

    /** \brief Aliases the common prefix of `reply` and `item`. */
    struct kbdus_reply_or_item_common common;
};

/** \brief The "type" of all kbdus-specific `ioctl` commands. */
#define KBDUS_IOCTL_TYPE 0xbd

/**
 * \brief Writes kbdus' version into the argument.
 *
 * If this ioctl fails, its argument is left in an unspecified state.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy to user space fails.
 * - Fails with `errno = EINTR` if interrupted.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_GET_VERSION _IOW(KBDUS_IOCTL_TYPE, 0, struct kbdus_version)

/**
 * \brief Creates a device and attaches the file description to that device.
 *
 * Note that the only way to detach a file description from a device is to close
 * that file description (*i.e.*, close all file descriptors pointing to that
 * file description).
 *
 * If the file description fails to attach to the device, then the device is
 * unconditionally destroyed before the ioctl returns.
 *
 * The given configuration is adjusted as specified in the documentation for
 * `struct kbdus_device_config` and `struct kbdus_fd_config`.
 *
 * If this ioctl fails with errno = EINTR, its argument is left unmodified. If
 * this ioctl fails with any other errno, its argument is left in an unspecified
 * state.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from/to user space fails.
 * - Fails with `errno = EINVAL` if the file description is concurrently
 *   attaching or is already attached to a device.
 * - Fails with `errno = EINVAL` if the given configuration is invalid.
 * - Fails with `errno = ENOSPC` if the maximum number of BDUS devices currently
 *   exist.
 * - Fails with `errno = EINTR` if interrupted.
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_CREATE_DEVICE                                              \
    _IOWR(KBDUS_IOCTL_TYPE, 1, struct kbdus_device_and_fd_config)

/**
 * \brief Attaches the file description to a given device.
 *
 * Note that the only way to detach a file description from a device is to close
 * that file description (*i.e.*, close all file descriptors pointing to that
 * file description).
 *
 * The id of the device is taken from the `device.id` field of the configuration
 * given as the ioctl argument. Other fields of `device` are not inspected, and
 * the `device` field is overwritten with the existing device's configuration.
 * *The configuration of the existing device is not modified in any way.
 *
 * The `fd` field of the configuration given as the ioctl argument can be
 * completely different of that of file descriptions previously attached to the
 * existing device.
 *
 * A file description can only be attached to devices that are already available
 * to clients. Nevertheless, the first request received through the file
 * description is always a "device available" notification.
 *
 * The device may or may not already have a file description attached to it. If
 * it does, (1) a single "flush+terminate" message, followed by "terminate"
 * messages ad infinitum, will be sent to that file description, (2) this ioctl
 * will block until that file description is closed, and (3) only then the new
 * file description will be attached to the device. However, in this case, if
 * the previous file description was not marked as successful prior to being
 * closed, or if this ioctl fails and the device is non-recoverable, it will be
 * destroyed.
 *
 * If this ioctl fails with errno = EINTR, its argument is left unmodified. If
 * this ioctl fails with any other errno, its argument is left in an unspecified
 * state.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from/to user space fails.
 * - Fails with `errno = EINVAL` if the file description is concurrently
 *   attaching or is already attached to a device.
 * - Fails with `errno = EINVAL` if no device with the given id ever existed.
 * - Fails with `errno = ENODEV` if the device with the given id doesn't exist
 *   anymore.
 * - Fails with `errno = EBUSY` if the device is not yet available to clients.
 * - Fails with `errno = EINPROGRESS` if another file description is trying to
 *   attach to the device.
 * - Fails with `errno = EINTR` if interrupted while waiting for
 *   already-attached file description to be closed. (Note that if the device is
 *   non-recoverable, it may then happen to be destroyed before the caller has a
 *   chance to retry this ioctl.)
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_ATTACH_TO_DEVICE                                           \
    _IOWR(KBDUS_IOCTL_TYPE, 2, struct kbdus_device_and_fd_config)

/**
 * \brief Terminates the file description.
 *
 * After this command is issued, "terminate" messages will be sent to the file
 * description ad infinitum.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EINVAL` if the file description is not attached to a
 *   device.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_TERMINATE _IO(KBDUS_IOCTL_TYPE, 3)

/**
 * \brief Marks the file description as successful.
 *
 * Whether or not a file description attached to a device was marked as
 * successful prior to being closed is relevant if the device is non-recoverable
 * and another file description is attaching to it. In that case, the device is
 * destroyed and the new file description will consequently fail to attach to
 * it.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EINVAL` if the file description is not attached to a
 *   device.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_MARK_AS_SUCCESSFUL _IO(KBDUS_IOCTL_TYPE, 4)

/**
 * \brief Obtains the numerical identifier of the device referred by a block
 *        special file.
 *
 * The argument is interpreted as the result of converting to a uint64_t a
 * pointer to a character buffer containing the path to the block special file.
 * Upon success, the argument is replaced with the device's numerical
 * identifier.
 *
 * If this ioctl fails with errno = EINTR, its argument is left unmodified. If
 * this ioctl fails with any other errno, its argument is left in an unspecified
 * state.
 *
 * Errors and return values:
 *
 * - Fails with errno = EFAULT if memory copy from/to user space fails.
 * - Fails with errno = EACCES, ELOOP, ENOENT, or ENOTDIR due to the same
 *   reasons as stat() does.
 * - Fails with errno = ENOTBLK if not a block special file.
 * - Fails with errno = EINVAL if the given dev_t can never correspond to a BDUS
 *   device or partition thereof.
 * - Fails with errno = ENODEV if the given dev_t is not in the minor space for
 *   an existing BDUS device.
 * - Fails with errno = ECHILD if the given dev_t is in the minor space of an
 *   existing BDUS device but it does not correspond to the whole device (i.e.,
 *   corresponds to a partition of that device that may or may not exist).
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_DEVICE_PATH_TO_ID _IOWR(KBDUS_IOCTL_TYPE, 5, uint64_t)

/**
 * \brief Retrieves the configuration of a device.
 *
 * The id of the device whose configuration to retrieve must be specified in the
 * `id` field of the argument. The entire argument is then overwritten with the
 * device's configuration.
 *
 * If this ioctl fails with errno = EINTR, its argument is left unmodified. If
 * this ioctl fails with any other errno, its argument is left in an unspecified
 * state.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from/to user space fails.
 * - Fails with `errno = EINVAL` if no device with the given id ever existed.
 * - Fails with `errno = ENODEV` if the device with the given id no longer
 *   exists.
 * - Fails with `errno = EINTR` if interrupted.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_GET_DEVICE_CONFIG                                          \
    _IOWR(KBDUS_IOCTL_TYPE, 6, struct kbdus_device_config)

/**
 * \brief Submits a flush request to the device with the given id and awaits its
 *        completion.
 *
 * This command has the effect of ensuring (if not interrupted) that all data
 * written to the device with the given id is persistently stored, as if fsync()
 * or fdatasync() were called on it.
 *
 * This command works even if the device is already open with O_EXCL.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from user space fails.
 * - Fails with `errno = EINVAL` if no device with the given id ever existed.
 * - Fails with `errno = ENODEV` if the device with the given id no longer
 *   exists.
 * - Fails with `errno = EINTR` if interrupted.
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_FLUSH_DEVICE _IOR(KBDUS_IOCTL_TYPE, 7, uint64_t)

/**
 * \brief Triggers the destruction of the device with the given id.
 *
 * If the device has a file description attached to it, this will cause
 * "terminate" messages to be sent to it ad infinitum. After it is closed, or if
 * the device does not have a file description attached, the device will be
 * destroyed (even if the device is recoverable).
 *
 * This command only triggers the above procedure, returning immediately without
 * waiting for any file description to be closed or for the device to be
 * destroyed.
 *
 * If the device with the given id no longer exists, this command has no effect.
 *
 * This does not flush the device.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from user space fails.
 * - Fails with `errno = EINVAL` if no device with the given id ever existed.
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION                                 \
    _IOR(KBDUS_IOCTL_TYPE, 8, uint64_t)

/**
 * \brief Blocks until the device with the given id is destroyed.
 *
 * If the device with the given id no longer exists, this command returns
 * immediately.
 *
 * Errors and return values:
 *
 * - Fails with `errno = EFAULT` if memory copy from user space fails.
 * - Fails with `errno = EINVAL` if no device with the given id ever existed.
 * - Fails with `errno = EINTR` if interrupted.
 * - May fail due to other unspecified reasons with other `errno` values as
 *   well.
 * - Returns 0 on success.
 */
#define KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED                             \
    _IOR(KBDUS_IOCTL_TYPE, 9, uint64_t)

/**
 * \brief Blocks until an item is ready to be consumed.
 *
 * The argument is the index of the `union kbdus_reply_or_item` to which to
 * write the item.
 *
 * If this ioctl fails with errno = EINTR, the struct kbdus_reply_or_item is
 * left unmodified. If this ioctl fails with any other errno, the struct
 * kbdus_reply_or_item is left in an unspecified state.
 */
#define KBDUS_IOCTL_RECEIVE_ITEM _IO(KBDUS_IOCTL_TYPE, 10)

/**
 * \brief Sends a reply to a (request) item.
 *
 * The argument is the index of the `union kbdus_reply_or_item` serving as the
 * reply.
 *
 * If the reply's `handle_index` field is 0, this has no effect.
 *
 * The struct kbdus_reply_or_item is always left unmodified.
 */
#define KBDUS_IOCTL_SEND_REPLY _IO(KBDUS_IOCTL_TYPE, 11)

/**
 * \brief Sends a reply to a request and then blocks until an item is ready to
 *        be consumed.
 *
 * If the reply's `handle_index` field is 0, no reply is sent.
 *
 * This may only fail with `EINTR` after the reply was successfully sent, so
 * when retrying the `handle` might first be set to 0, but it's not strictly
 * necessary since attempts to complete an already completed request are
 * ignored.
 *
 * If this ioctl fails with errno = EINTR, the struct kbdus_reply_or_item is
 * left unmodified. If this ioctl fails with any other errno, the struct
 * kbdus_reply_or_item is left in an unspecified state.
 */
#define KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM _IO(KBDUS_IOCTL_TYPE, 12)

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------- */

#endif /* KBDUS_HEADER_KBDUS_H_ */
