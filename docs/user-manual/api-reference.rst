.. .......................................................................... ..

.. _api-reference:

API reference
=============

This page contains reference documentation for the entire C99 API of *libbdus*, BDUS' user-space driver development library, which is exported by the ``bdus.h`` header file.

For a more introductory description of the driver development interface and features provided by BDUS, see :ref:`developing-drivers`.

.. rubric:: Summary

- :ref:`api-driver-development`:

  - :func:`bdus_run`
  - :func:`bdus_rerun`
  - :type:`struct bdus_ctx <bdus_ctx>`
  - :type:`struct bdus_ops <bdus_ops>`
  - :type:`struct bdus_attrs <bdus_attrs>`
  - :enumerator:`bdus_abort`

- :ref:`api-device-management`:

  - :func:`bdus_get_dev_id_from_path`
  - :func:`bdus_flush_dev`
  - :func:`bdus_destroy_dev`

- :ref:`api-errors`:

  - :func:`bdus_get_error_message`

- :ref:`api-versions`:

  - :type:`struct bdus_version <bdus_version>`
  - :func:`bdus_get_kbdus_version`
  - :func:`bdus_get_libbdus_version`
  - :macro:`BDUS_HEADER_VERSION_{MAJOR,MINOR,PATCH} <BDUS_HEADER_VERSION_MAJOR>`
  - :macro:`BDUS_REQUIRE_VERSION_{MAJOR,MINOR,PATCH} <BDUS_REQUIRE_VERSION_MAJOR>`

.. .......................................................................... ..

.. _api-driver-development:

Driver development
------------------

.. doxygenfunction:: bdus_run
.. doxygenfunction:: bdus_rerun
.. doxygenstruct:: bdus_ctx
.. doxygenstruct:: bdus_ops
.. doxygenstruct:: bdus_attrs
.. doxygenenumvalue:: bdus_abort

.. .......................................................................... ..

.. _api-device-management:

Device management
-----------------

.. doxygenfunction:: bdus_get_dev_id_from_path
.. doxygenfunction:: bdus_flush_dev
.. doxygenfunction:: bdus_destroy_dev

.. .......................................................................... ..

.. _api-errors:

Errors
------

.. doxygenfunction:: bdus_get_error_message

.. .......................................................................... ..

.. _api-versions:

Versions
--------

.. doxygenstruct:: bdus_version
.. doxygenfunction:: bdus_get_kbdus_version
.. doxygenfunction:: bdus_get_libbdus_version

.. doxygendefine:: BDUS_HEADER_VERSION_MAJOR
.. doxygendefine:: BDUS_HEADER_VERSION_MINOR
.. doxygendefine:: BDUS_HEADER_VERSION_PATCH

.. doxygendefine:: BDUS_REQUIRE_VERSION_MAJOR
.. doxygendefine:: BDUS_REQUIRE_VERSION_MINOR
.. doxygendefine:: BDUS_REQUIRE_VERSION_PATCH

.. .......................................................................... ..
