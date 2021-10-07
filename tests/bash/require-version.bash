# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that compilation of a driver fails if it requires an
# incompatible BDUS version, and succeeds otherwise.

# ---------------------------------------------------------------------------- #

function get_driver()
{
    echo "
        #define BDUS_REQUIRE_VERSION_MAJOR $1
        #define BDUS_REQUIRE_VERSION_MINOR $2
        #define BDUS_REQUIRE_VERSION_PATCH $3

        #include <bdus.h>

        int main(void) { }
        "
}

function expect_ok()
{
    driver="$( get_driver "$@" )"
    compile_c_to driver /dev/null
}

function expect_bad()
{
    driver="$( get_driver "$@" )"
    { ! compile_c_to driver /dev/null; } |&
        grep "incompatible version was required"
}

# expect_ok

expect_ok \
    'BDUS_HEADER_VERSION_MAJOR' \
    'BDUS_HEADER_VERSION_MINOR' \
    '0'

expect_ok \
    'BDUS_HEADER_VERSION_MAJOR' \
    'BDUS_HEADER_VERSION_MINOR' \
    'BDUS_HEADER_VERSION_PATCH'

# expect_bad

expect_bad \
    'BDUS_HEADER_VERSION_MAJOR' \
    'BDUS_HEADER_VERSION_MINOR' \
    '(BDUS_HEADER_VERSION_PATCH + 1)'

expect_bad \
    'BDUS_HEADER_VERSION_MAJOR' \
    '(BDUS_HEADER_VERSION_MINOR + 1)' \
    '0'

expect_bad \
    'BDUS_HEADER_VERSION_MAJOR' \
    '(BDUS_HEADER_VERSION_MINOR - 1)' \
    '0'

expect_bad 1 0 0

# ---------------------------------------------------------------------------- #
