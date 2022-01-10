.. .......................................................................... ..

.. _block-devices:

Block devices
=============

This section overviews the block device abstraction under Linux and details some aspects of its internal operation that are relevant when developing drivers.

.. .......................................................................... ..

The block device abstraction
----------------------------

As explained in the :ref:`quick-start-guide`, block devices provide access to storage systems that present data as a contiguous sequence of bytes partitioned into fixed-size blocks, and are ubiquitously used to expose local and remote storage devices.
Block devices hide the intricacies of each particular storage system or device, exposing them to applications through a uniform interface.

An application may, for instance, open a block device with the ``open()`` system call and subsequently ``read()`` from and ``write()`` to it, irrespective of the device's underlying implementation.
The Linux kernel itself can also make use of block devices (*e.g.*, as the backing store for a local file system), although the kernel interfaces used to access them are distinct from those available to user-space programs.

Either way, operations on block devices are translated into *requests* and sent to the corresponding device driver, which must then process them appropriately and send a response back to the client that submitted them.
These requests may be of several *types*, encoding the specific operation that was requested (*e.g.*, reading data, writing data, discarding previously written data).
Different block devices may support different sets of request types.

Besides the types of request that it supports, each block device is also characterized by some *attributes*, the most obvious being its *size* --- the number of bytes that the device can store.
One other relevant attribute is the *logical block size*, which determines the lowest possible size that the device is able to address.
Yet another example is the maximum size of read or write requests that the driver is able to process.

However, with the natural exception of the device size, clients need not in general concern themselves with these attributes, as the Linux kernel transparently adjusts requests so that the appropriate requirements are met and the driver can process them (*e.g.*, by splitting an operation into two or more requests).

.. .......................................................................... ..

Request types
-------------

We now describe the semantics of several types of block device requests.

.. rubric:: Simple data transfer requests

The two most basic types of request that a driver can receive are **read** and **write** requests.
As expected, these are used to read data from, and write data to, the device.
These requests are generated when an application uses the ``read()`` and ``write()`` system calls on a block device, for instance.

However, *write* requests are not the only manner of transferring data to the device: **write same** requests can be used to write the same data to several contiguous logical blocks of the device.
When applicable, this avoids the overhead of creating and filling large data buffers or submitting many smaller requests.

An even more specialized variation of the *write* request is the **write zeros** request, which is used to fill a range of the device with zeros.
User-space applications can submit requests of this type by performing an ``ioctl()`` call with the ``BLKZEROOUT`` command on the block device.

If *write same* or *write zeros* requests are submitted to a driver that does not support them, they are transparently converted by the Linux kernel into equivalent *write* requests.

.. rubric:: Cache management requests

Many storage devices feature internal volatile caches.
Although this can improve performance, it may also lead to crash-resilience issues if the cache has a *write back* policy, meaning that data written to the cache is not immediately persisted.
Applications must therefore take appropriate measures if they wish to guarantee that data has been persistently stored.

One such measure is the **flush** request, which causes all data written previously to the device to be fully persisted and only returns once this is accomplished.
One way of submitting *flush* requests is by invoking the ``fsync()`` system call on the block device.

However, if the user only needs to ensure that a specific write operation has been persisted, then issuing a *Force Unit Access* write, or **FUA write** request, is a more appropriate option.
This type of request behaves like a regular *write* request, but only completes when the written data is safely persisted on the device.
Compared to a *flush* request, it avoids the overhead of flushing the entire cache.

If a block device driver supports *flush* requests but not *FUA write* requests, the latter will be transparently converted into a *write* request followed by a *flush* request.
However, if both *flush* and *FUA write* requests are not supported by the driver, it is assumed that the device does not feature an internal write-back cache.

.. rubric:: Data discarding requests

Another type of request is **discard**, which informs the driver that the contents of a given range of the device are no longer needed.
Reads performed on a discarded region then return unspecified data until it is again written to.
This is relevant for instance in thin provisioned systems, where these requests allow space associated with the discarded range to be deallocated, and for SSD devices, where they are converted into TRIM commands.

A variant of this request type is **secure erase**, which functions much like *discard* but also ensures that reads to the erased area do not return its previous contents.

.. rubric:: *ioctl* requests

Finally, block devices can also receive ``ioctl()`` calls.
Strictly speaking, these do not translate into block device requests.
However, for simplicity of presentation, and for the purposes of the BDUS framework, these are also referred to as requests.

.. rubric:: Summary

In summary, block device drivers may receive requests of the following types:

#. *Read* -- read data from the device;
#. *Write* -- write arbitrary data to the device;
#. *Write same* -- write the same data to several contiguous logical blocks of the device;
#. *Write zeros* -- write zeros to a range of the device;
#. *FUA write* -- write arbitrary data to the device and immediately persist it;
#. *Flush* -- persist all previously written data;
#. *Discard* -- discard a range of data from the device;
#. *Secure erase* -- discard a range of data, ensuring that it can't be read back;
#. *ioctl* -- perform some arbitrary, driver-specific action.

There also a few request types dedicated to the management of `zoned devices <https://zonedstorage.io/docs/introduction/zoned-storage>`_.
We do not discuss these here as BDUS does not currently support this type of device.

.. .......................................................................... ..

Caches
------

.. figure:: /images/block-device-caches.*
    :align: right
    :width: 145px

    ..


We mentioned above that block devices may feature internal write-back caches.
This causes the data written by regular *write* requests to not be immediately persisted, but *flush* or *write FUA* requests can be used to ensure that data is safely stored.

However, the operating system itself also places a write-back cache in between block devices and their clients --- the *page cache*.
With this cache, ``read()`` operations can be served directly from memory without ever being translated into *read* requests and without necessitating the intervention of the block device driver (if the requested data is in the cache).
Similarly, ``write()`` operations modify the content of the page cache, but might not immediately be translated into *write* requests and submitted to the block device for processing.

This can improve performance, but leads to the same data volatility issues mentioned above (*e.g.*, data written to the cache but not propagated to the device is lost if the system crashes).
Thankfully, invoking ``fsync()`` on the block device also flushes the page cache, submitting *write* requests for all unwritten data in the cache, in addition to submitting a *flush* request.

Note that applications can also completely bypass the page cache by passing the ``O_DIRECT`` flag to ``open()`` when opening the block device.
This way, all ``read()`` and ``write()`` operations are directly converted into *read* and *write* requests and completed synchronously.
In this case however, the application is required to only submit requests fully aligned to the device's logical block size.
Note also that ``O_DIRECT`` does not preclude the necessity of using ``fsync()`` to submit *flush* requests, as it only bypasses the page cache, not the internal device caches.

.. .......................................................................... ..

Partitions
----------

.. note::

    This section is under construction.

.. .......................................................................... ..
