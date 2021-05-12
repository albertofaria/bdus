.. .......................................................................... ..

BDUS: Block Devices in User Space
=================================

|build-badge| |version-badge| |license-badge|

BDUS is a Linux 4.0+ framework for implementing **block devices in user space**, enabling you to create block device drivers as regular C programs.
It was introduced by the SYSTOR '21 paper `"BDUS: Implementing Block Devices in User Space" <https://doi.org/10.1145/3456727.3463768>`_.

This is the entry page for BDUS' documentation, which is divided into three parts:

* The :ref:`quick-start-guide`, introducing block devices and BDUS;
* The :ref:`user-manual`, describing BDUS' capabilities and API for driver development;
* The :ref:`developer-manual`, detailing BDUS' internals and contribution procedures.

To get started, take a look at the :ref:`quick-start-guide`.
You might also be looking for the :ref:`api-reference`.

**Installation**
    Download `BDUS' latest release <release-tar_>`_ and run ``sudo make install``.
    For more details, see :ref:`installation`.

**Licensing**
    BDUS is distributed under the terms of the :repo-file:`MIT license <LICENSE-MIT.txt>`, with the exception of its kernel module which is distributed under the terms of the :repo-file:`GPLv2 license <LICENSE-GPLv2.txt>`.
    For more details, see :ref:`licensing`.

**Contributing**
    To report bugs, suggest improvements, or propose new features, please use GitHub's `issue tracking system <https://github.com/albertofaria/bdus/issues>`_.
    For information on how to contribute changes, see :ref:`contributing`.

**Citing**
  When citing BDUS, please reference:
  `Alberto Faria, Ricardo Macedo, José Pereira, João Paulo. 2021. BDUS: Implementing Block Devices in User Space. In Proceedings of the 14th ACM International Conference on Systems and Storage (SYSTOR '21). <https://doi.org/10.1145/3456727.3463768>`_

.. .......................................................................... ..

.. toctree::
    :hidden:

    self

.. toctree::
    :hidden:
    :includehidden:
    :maxdepth: 3
    :numbered: 2

    quick-start-guide
    user-manual/index
    developer-manual/index

.. .......................................................................... ..
