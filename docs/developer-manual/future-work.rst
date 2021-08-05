.. .......................................................................... ..

.. _future-work:

Future work
===========

This section contains a TODO list, and enumerates possible improvements and several ideas for future development.

.. .......................................................................... ..

TODO list
---------

Bug fixes
~~~~~~~~~

- **Fix race condition between 'rmmod' and 'bdus destroy'.**
  Unloading *kbdus* immediately after destroying a device through ``bdus destroy`` or ``bdus_destroy_dev()`` can fail due to *kbdus* still being in use.
  This is because the return from ``KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED`` is triggered from the control device's ``release()`` operation, which is a tiny bit before the file description reference is actually dropped.
  How can this be fixed?

New features
~~~~~~~~~~~~

- **Add support for implementing drivers exporting zoned block devices.**
  This would amount to adding a few more request types and attributes.

- **Allow drivers to allocate their own request payload buffers.**
  This is useful when using SPDK, for instance, which exposes its own memory allocation interface that pins memory to a physical location.

- **Add an asynchronous driver development interface.**
  This would ideally be a passive interface, in the sense that no internal threads would be spawned and all BDUS code should run directly in the context of functions invoked by the driver.
  In particular, it should be possible to share the same request payload buffers between concurrently-running drivers.
  Would also be nice to share threads (execution contexts for callbacks) between drivers.

- **Add support for splicing,** which transfers data between file descriptors without copying it to user space.
  May improve performance for drivers implementing, for instance, deduplication, where it would still have to look at the written content but for read requests it could just splice the data from the underlying store.
  See `splice(2) <https://man7.org/linux/man-pages/man2/splice.2.html>`_.

Interface and implementation improvements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **Make command `bdus destroy` accept more than one device,** destroying each in sequence.

- **Improve test quality and coverage.**
  We currently have a simple test harness to run Bash scripts and C programs, but few tests.

- **Possibly support deep copying of ioctl arguments,** similar to what FUSE supports.
  Maybe also support commands that don't follow the correct ``_IO``/``_IOW``/``_IOR``/``_IOWR`` encoding.

Performance and resource utilization improvements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **Use io_uring for user-kernel communication.**
  Experiment with its polling capabilities to mitigate context switching delays, hopefully reducing latencies and improving performance.
  See the `io_uring whitepaper <https://kernel.dk/io_uring.pdf>`_ and `this nice guide <https://unixism.net/loti/index.html>`_.
  Implementation-wise, this would likely begin with supporting reads and writes on the control device for communication.
  *libbdus* would then use io_uring to operate on the control device, falling back to the current implementation if the system's kernel does not support io_uring.

- **Dynamically change the number of worker threads** and associated resources according to utilization, up to the maximum number of threads set in the driver attributes.

.. .......................................................................... ..

Ideas and notes
---------------

- **Consider using DKMS,** either by default or when requested by the user.
  See the `official GitHub repository <https://github.com/dell/dkms>`_.

- **Find some way to prevent false aliasing of callback buffers.**
  The ``char *buffer`` parameter to :member:`bdus_ops.read` and :member:`bdus_ops.write` callbacks aliases other pointers.
  This may prevent some compiler optimizations, as it can think that, *e.g.*, ``buffer`` aliases data accessed through ``dev`` or ``dev->private_data``.
  Switching the parameter type to ``void *`` and casting its uses to ``char *`` when necessary does not appear to solve the issue.
  Adding ``restrict`` to the ``buffer`` parameter also is not appropriate, as it tells the compiler that the data accessible through the pointer *is not accessed in any other manner*, which might not be true.
  Is there any good solution to this problem?
  Is this a significant problem at all?

.. .......................................................................... ..
