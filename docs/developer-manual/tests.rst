.. .......................................................................... ..

.. _tests:

Tests
=====

Directory :repo-dir:`tests/` contains a simple test harness to run tests written as Bash scripts or C programs.
However, test coverage is low.
Improvements in this area would be very welcome.

GitHub Actions is used to run all tests on every push and pull request.
This is configured in the :repo-file:`.github/workflows/build.yml` file, and the setup also ensures that:

#. Code is correctly formatted;
#. Occurrences of the current version number are coherent;
#. The documentation build succeeds without warnings and has no broken links;
#. *libbdus* and the ``bdus`` command compile without warnings; and
#. *kbdus* compiles without warnings on every supported kernel version.

.. .......................................................................... ..
