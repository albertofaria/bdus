.. .......................................................................... ..

.. _licensing:

Licensing
=========

BDUS consists of three components:

#. *kbdus*, a Linux kernel module used internally by BDUS;
#. *libbdus*, a user-space library providing the C API for driver development;
#. The ``bdus`` command, for managing devices created using BDUS.

*libbdus* and the ``bdus`` command are distributed under the terms of the :repo-file:`MIT license <LICENSE-MIT.txt>`.
However, *kbdus* is distributed under the terms of the :repo-file:`GPLv2 license <LICENSE-GPLv2.txt>`, due to restrictions regarding the licensing of Linux kernel modules.
Nevertheless, the use of ``kbdus`` through its user-space API is *not* subject to the terms of the GPLv2 license, since the use of kernel services through system calls "does not fall under the heading of derived work" (see https://spdx.org/licenses/Linux-syscall-note.html).

This means that users of the BDUS framework are subject only to the terms of the MIT license, even when creating language bindings or other applications that interface directly with the *kbdus* module.

Specifically, in release artifacts and in the repository, all files under the ``kbdus/`` directory, *with the exception of* ``kbdus/include/kbdus.h``, are distributed under the terms of the GPLv2 license.
All other files (including ``kbdus/include/kbdus.h``) are distributed under the terms of the MIT license.

Below are reproductions of the MIT and GPLv2 licenses as found in files :repo-file:`LICENSE-MIT.txt` and :repo-file:`LICENSE-GPLv2.txt` in the repository.

----

.. rubric:: MIT license

.. include:: ../../LICENSE-MIT.txt
    :literal:

----

.. rubric:: GPLv2 license

.. include:: ../../LICENSE-GPLv2.txt
    :literal:

.. .......................................................................... ..
