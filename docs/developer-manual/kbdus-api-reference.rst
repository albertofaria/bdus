.. .......................................................................... ..

.. _kbdus-api-reference:

kbdus API reference
===================

This page contains reference documentation for the entire user-space API of *kbdus*, which is exported by the ``kbdus.h`` header file.

.. NOTE: Use :no-link: to prevent these symbols from clobbering search results.

.. rubric:: Summary

- :ref:`kbdus-driver-life-cycle`:

  - ``struct kbdus_device_config``
  - ``struct kbdus_fd_config``
  - ``struct kbdus_device_and_fd_config``

  - ``KBDUS_IOCTL_CREATE_DEVICE``
  - ``KBDUS_IOCTL_ATTACH_TO_DEVICE``
  - ``KBDUS_IOCTL_TERMINATE``
  - ``KBDUS_IOCTL_MARK_AS_SUCCESSFUL``

- :ref:`kbdus-items-and-replies`:

  - ``enum kbdus_item_type``
  - ``struct kbdus_item``
  - ``struct kbdus_reply``
  - ``struct kbdus_reply_or_item_common``
  - ``union kbdus_reply_or_item``

  - ``KBDUS_IOCTL_RECEIVE_ITEM``
  - ``KBDUS_IOCTL_SEND_REPLY``
  - ``KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM``

- :ref:`kbdus-device-management`:

  - ``KBDUS_IOCTL_DEVICE_PATH_TO_ID``
  - ``KBDUS_IOCTL_GET_DEVICE_CONFIG``
  - ``KBDUS_IOCTL_FLUSH_DEVICE``
  - ``KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION``
  - ``KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED``

- :ref:`kbdus-versions`:

  - ``struct kbdus_version``
  - ``KBDUS_IOCTL_GET_VERSION``
  - ``KBDUS_HEADER_VERSION_{MAJOR,MINOR,PATCH}``

- :ref:`kbdus-miscellaneous`:

  - ``KBDUS_IOCTL_TYPE``

.. .......................................................................... ..

.. _kbdus-driver-life-cycle:

Driver life cycle
-----------------

.. doxygenstruct:: kbdus_device_config
    :project: kbdus
    :no-link:

.. doxygenstruct:: kbdus_fd_config
    :project: kbdus
    :no-link:

.. doxygenstruct:: kbdus_device_and_fd_config
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_CREATE_DEVICE
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_ATTACH_TO_DEVICE
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_TERMINATE
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_MARK_AS_SUCCESSFUL
    :project: kbdus
    :no-link:

.. .......................................................................... ..

.. _kbdus-items-and-replies:

Items and replies
-----------------

.. doxygenenum:: kbdus_item_type
    :project: kbdus
    :no-link:

.. doxygenstruct:: kbdus_item
    :project: kbdus
    :no-link:

.. doxygenstruct:: kbdus_reply
    :project: kbdus
    :no-link:

.. doxygenstruct:: kbdus_reply_or_item_common
    :project: kbdus
    :no-link:

.. doxygenunion:: kbdus_reply_or_item
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_RECEIVE_ITEM
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_SEND_REPLY
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_SEND_REPLY_AND_RECEIVE_ITEM
    :project: kbdus
    :no-link:

.. .......................................................................... ..

.. _kbdus-device-management:

Device management
-----------------

.. doxygendefine:: KBDUS_IOCTL_DEVICE_PATH_TO_ID
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_GET_DEVICE_CONFIG
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_FLUSH_DEVICE
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_TRIGGER_DEVICE_DESTRUCTION
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_WAIT_UNTIL_DEVICE_IS_DESTROYED
    :project: kbdus
    :no-link:

.. .......................................................................... ..

.. _kbdus-versions:

Versions
--------

.. doxygenstruct:: kbdus_version
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_IOCTL_GET_VERSION
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_HEADER_VERSION_MAJOR
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_HEADER_VERSION_MINOR
    :project: kbdus
    :no-link:

.. doxygendefine:: KBDUS_HEADER_VERSION_PATCH
    :project: kbdus
    :no-link:

.. .......................................................................... ..

.. _kbdus-miscellaneous:

Miscellaneous
-------------

.. doxygendefine:: KBDUS_IOCTL_TYPE
    :project: kbdus
    :no-link:

.. .......................................................................... ..
