.. .......................................................................... ..

.. _quick-start-guide:

Quick Start Guide
=================

This guide briefly introduces block devices and shows how to use BDUS to implement the driver for a simple RAM device.

Other parts of the documentation:

* The :ref:`user-manual`, describing BDUS' capabilities and API for driver development;
* The :ref:`developer-manual`, detailing BDUS' internals and contribution procedures.

.. .......................................................................... ..

Block devices
-------------

*Block devices* are an abstraction present in Unix-based operating systems that provides access to storage systems that transfer randomly accessible data in fixed-size blocks, presenting data as a contiguous byte sequence, and are ubiquitously used to expose local and remote storage devices.

Many applications rely on block devices to store data, either by using them directly or through some other system that relies on block devices, such as a file system.
For this reason, storage systems that expose a block device interface can be leveraged by existing applications without modification.

For a storage system to provide such an interface, one would typically need to implement it as a kernel-level operating system driver, which is an arduous and error-prone task.
This is where BDUS comes in: it enables you to implement the behavior of a block device in user space.

In more detail, block devices are made available through *block special files* typically created under ``/dev``, and applications can read from and write to them as if they were regular files.
Doing so causes requests to be submitted to the block device's driver, which must then handle them.
With BDUS you can implement this driver as a regular user-space program written in C, and process each request according to your own logic.

.. .......................................................................... ..

Installing BDUS
---------------

To install BDUS, download its `latest stable release <release-tar_>`_, extract it, and use the provided Makefile by running the following command:

.. code-block:: console

    $ sudo make install

To revert all changes made by the installation, you can use this command:

.. code-block:: console

    $ sudo make uninstall

.. .......................................................................... ..

Writing a driver
----------------

To demonstrate the basics of using BDUS to implement block device drivers, we are going to develop the driver for a simple RAM-backed device, *i.e.*, a block device that stores its contents in the system's RAM.

We must first include the ``bdus.h`` header file to gain access to BDUS' API, and also some standard C headers that we are going to use:

.. code-block:: c

    #include <bdus.h>

    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

To specify the behavior of our device, we have to implement its *operations*.
These are callback functions that will be invoked by BDUS whenever a request is received.

There are several types of operations that a driver can implement.
For our simple RAM device, implementing the ``read`` and ``write`` operations suffices:

.. code-block:: c

    static int device_read(
        char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        memcpy(buffer, (char *)ctx->private_data + offset, size);
        return 0;
    }

    static int device_write(
        const char *buffer, uint64_t offset, uint32_t size,
        struct bdus_ctx *ctx
        )
    {
        memcpy((char *)ctx->private_data + offset, buffer, size);
        return 0;
    }

The ``device_read`` function will be called whenever a *read* request is submitted to our device, while ``device_write`` will be invoked whenever a *write* request is received.
Their parameters have the following meanings:

* ``buffer``: a buffer to which the requested data should be read, or whose contents should be written to the device;
* ``offset``: the offset (in bytes) into the device at which the read or write should take place;
* ``size``: the number of bytes that should be read or written;
* ``ctx``: context information about the device and driver that received the request.

For our RAM device to work, we would first need to allocate a memory buffer where the device's contents would be stored.
For now, we assume that a pointer to such a buffer is available in these callbacks as ``ctx->private_data``.
Since this pointer has type ``void *``, we cast it to ``char *`` to be able to add the offset to it.

The implementations of the two functions above are straightforward: they simply copy data between the request buffer and our RAM buffer.
As these operations can never fail, we ``return 0`` to indicate that all went well.

All callbacks for a device must be stored in a value of type :type:`struct bdus_ops <bdus_ops>`, which will later be passed on to BDUS:

.. code-block:: c

    static const struct bdus_ops device_ops =
    {
        .read  = device_read,
        .write = device_write,
    };

Using this type of initialization, the remaining fields of the structure are set to ``NULL``, which will inform BDUS that our driver does not support other types of requests.

.. note::

    You should **always** zero-initialize the ``bdus_ops`` and ``bdus_attrs`` structs, even if you manually initialize all fields, so that your program remains correct with future BDUS versions that add more fields.

Now that we have implemented our device's operations, we have to configure some of its *attributes*.
Among these are the device's *size* --- the number of bytes that the device can store --- and *logical block size* --- the smallest size that the driver is able to address.
Requests submitted to our driver will always be aligned to the logical block size.

We'll set our device's size to the arbitrary value of 1 GiB.
Also, for our simple driver, setting its logical block size to the minimum allowed value of 512 bytes is adequate.

To later pass these attributes to BDUS, we have to store them in a value of type :type:`struct bdus_attrs <bdus_attrs>`:

.. code-block:: c

    static const struct bdus_attrs device_attrs =
    {
        .size               = 1 << 30, // 1 GiB
        .logical_block_size = 512,
    };

By using this type of initialization again, we ensure that the remaining fields of the structure are set to zero, which will later instruct BDUS to pick default values for them.
The two attributes that we have set are the only mandatory ones.

Note that several restrictions are imposed on the values of these attributes.
For more details, check the documentation for :type:`struct bdus_attrs <bdus_attrs>`.

Finally, and because our driver is meant to be compiled as an executable program, we have to implement the ``main()`` function:

.. code-block:: c

    int main(void)
    {
        void *buffer = malloc(device_attrs.size);

        if (!buffer)
            return 1;

        bool success = bdus_run(&device_ops, &device_attrs, buffer);

        free(buffer);

        if (!success)
            fprintf(stderr, "Error: %s\n", bdus_get_error_message());

        return success ? 0 : 1;
    }

Here, we first allocate the RAM buffer that will be used to store our device's contents, and then run the driver by calling :func:`bdus_run`.
This function receives the following arguments:

#. A pointer to the structure containing the device's operations;
#. A pointer to the structure containing the device's attributes;
#. The initial value for the ``ctx->private_data`` field that is available from the device's callbacks (this is why we used it as a pointer to our RAM buffer).

We then print an error message if :func:`bdus_run` failed, free the buffer, and exit.

That is it.
By concatenating all the previous code excerpts, you get a fully-functional, RAM-backed block device driver.
The full source code for this driver is available in the repository at :repo-file:`examples/ram.c`.
Other example drivers can also be found in the :repo-dir:`examples/` directory.

.. .......................................................................... ..

Using the driver
----------------

Now that our driver is implemented, we must compile it into an executable program.

Let's say that the driver's source code is entirely contained in file ``driver.c``.
In this case, you can compile it by typing the following command:

.. code-block:: console

    $ cc driver.c -lbdus -o driver

The ``-lbdus`` flag tells the linker to link our program against BDUS' driver development library.
Assuming that the program was compiled successfully, there should now be an executable file named ``driver`` in the current directory.
To create a block device powered by our driver, simply run that executable as the superuser:

.. code-block:: console

    $ sudo ./driver

If everything goes well, the path to our RAM device's block special file will be printed and you will regain control of the terminal.
From this point on, that block device is available for anyone (with sufficient privileges) to use.

Let's assume that everything did go alright, and that the driver printed ``/dev/bdus-0``.
We may now, for example, create a file system on that device:

.. code-block:: console

    $ sudo mkfs.ext4 /dev/bdus-0

This command will effectively create an `ext4 <https://en.wikipedia.org/wiki/Ext4>`_ file system that resides in the system's RAM.
We can then `mount <https://en.wikipedia.org/wiki/Mount_(computing)>`_ and use that file system:

.. code-block:: console

    $ mkdir mount-point
    $ sudo mount /dev/bdus-0 mount-point

To unmount the file system, use the following command:

.. code-block:: console

    $ sudo umount mount-point

After you finish playing with your new RAM device, you may want to remove it from your system.
To do so, run:

.. code-block:: console

    $ sudo bdus destroy /dev/bdus-0

This command ends execution of the driver and removes the device from ``/dev``.

.. .......................................................................... ..

What's next?
------------

You may now want to read the :ref:`user-manual`, which provides an in-depth description of BDUS' capabilities and API for driver development.
Its :ref:`api-reference` section is a particularly handy resource for those developing drivers with BDUS.

.. .......................................................................... ..
