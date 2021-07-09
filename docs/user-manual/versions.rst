.. .......................................................................... ..

.. _versions:

Versions
========

This section describes BDUS' versioning scheme and the backward compatibility guarantees provided across releases.
A list of existing releases and respective notable changes is also included.

.. .......................................................................... ..

.. _versioning-scheme:

Versioning scheme
-----------------

This project adheres to the `SemVer 2.0.0 <https://semver.org/spec/v2.0.0.html>`_ versioning scheme.
Releases are identified by a *version number* of the form **x.y.z**, where **x**, **y**, and **z** are the *major*, *minor*, and *patch* versions, respectively.

These version numbers encode *public API and ABI* backward compatibility guarantees across releases.
The user-space driver development API as documented in the :ref:`api-reference`, together with the interface provided by the ``bdus`` command, constitute BDUS' public API.

(The user-space interface exposed by the *kbdus* kernel module is also part of the public API.
This is most relevant to those creating bindings for other languages.
See :ref:`kbdus-api-reference` for more details.)

BDUS is currently in the "initial development" phase, meaning that every release has *major* version 0 and no API or ABI backward compatibility guarantees are provided between releases with distinct *minor* versions.
However, releases which differ only in the *patch* version are API and ABI backward compatible.
This means that drivers written and compiled against a given release can be run under a BDUS installation of any subsequent release with the same *minor* version.

Specifically, releases that break API or ABI backward compatibility with the preceding one trigger a *minor* version increment and reset the *patch* version to 0 (*e.g.*, 0.5.3 → 0.6.0).
Other releases instead trigger only a *patch* version increment (*e.g.*, 0.6.0 → 0.6.1).

As an important exception, API preconditions may be relaxed without this being considered a breaking change, *i.e.*, interface usages that in one release are erroneous may become valid in a subsequent, backward compatible release.
Error messages (but not error codes like ``errno`` values and exit statuses) can similarly change across backward compatible releases.

.. tip::

    To ensure that your driver only compiles against expected versions of BDUS and can run against any installation compatible with that version, use the :macro:`BDUS_REQUIRE_VERSION_{MAJOR,MINOR,PATCH} <BDUS_REQUIRE_VERSION_MAJOR>` macros.

.. rubric:: The plan for 1.0.0 and above

Starting with BDUS 1.0.0, releases will maintain API and ABI backward compatibility across *minor* versions.
The *minor* version will then be incremented for releases that extend the public API or ABI in any manner (and maybe also for releases with major implementation changes).

This means that drivers written and compiled against a given release with *major* version 1 will be able to run under a BDUS installation of any subsequent release that also has *major* version 1.

.. .......................................................................... ..

.. _version-history:

Version history
---------------

The following is a list of all releases up to BDUS |version|, in reverse chronological order.

0.1.1 (2021-07-09)
~~~~~~~~~~~~~~~~~~

The following is a list of notable changes relative to version 0.1.0.
See also the :diff:`git diff <v0.1.0...v0.1.1>`.

- *kbdus*: Expose the major and minor numbers of devices through fields :member:`kbdus_device_config.major` and :member:`kbdus_device_config.minor`.

- *libbdus*: Expose the major and minor numbers of devices through fields :member:`bdus_ctx.major` and :member:`bdus_ctx.minor`.

0.1.0 (2021-05-08)
~~~~~~~~~~~~~~~~~~

First open-source release.

.. .......................................................................... ..
