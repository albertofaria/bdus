.. .......................................................................... ..

.. _installation:

Installation
============

Releases can be downloaded from the GitHub repository's `Releases <https://github.com/albertofaria/bdus/releases>`_ section.
BDUS is compatible with Linux 4.0 and above.

.. .......................................................................... ..

Basic instructions
------------------

.. rubric:: Installing

BDUS can be installed using the Makefile included with every release.
Simply download and extract the desired release and run the following command:

.. code-block:: console

    $ sudo make install

This will install the *kbdus* kernel module, the *libbdus* driver development library, and the ``bdus`` command.
Previously installed versions of these components are overwritten.

Note that if you later switch to a different Linux kernel version, a reinstallation might be necessary.

.. rubric:: Uninstalling

The same Makefile can be used to uninstall BDUS.
Run the following command to revert all changes made by the installation:

.. code-block:: console

    $ sudo make uninstall

.. .......................................................................... ..

Advanced options
----------------

While the ``install`` Makefile target installs all three aforementioned components, the ``install-kbdus``, ``install-libbdus``, and ``install-cmdbdus`` targets can be used to install each component separately:

- ``install-kbdus`` installs the *kbdus* kernel module and the ``kbdus.h`` header file that exposes its user-space interface;

- ``install-libbdus`` installs the *libbdus* shared library and the ``bdus.h`` header file that exposes its API, and makes it so the ``-lbdus`` linker flag refers to the installed shared library;

- ``install-cmdbdus`` installs the ``bdus`` command.

Targets ``uninstall-kbdus``, ``uninstall-libbdus``, and ``uninstall-cmdbdus`` uninstall each component separately.

Finally, one can customize the directories to which BDUS is installed (or where it is uninstalled from) by setting the ``KBDUS_…_INSTALL_DIR``, ``LIBBDUS_…_INSTALL_DIR``, and ``CMDBDUS_…_INSTALL_DIR`` environment variables.
Check the :repo-file:`Makefile` to see how these are used.

.. .......................................................................... ..
