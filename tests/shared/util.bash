# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

function kernel_is_at_least()
{
    { (( $# == 1 )) && [[ "$1" =~ ^[0-9]+\.[0-9]+$ ]]; } || exit 1

    [[ "$1" = "$(
        printf '%s\n%s' "${kernel_major}.${kernel_minor}" "$1" |
            sort -V |
            head -n1
        )" ]]
}

function kernel_is_one_of()
{
    local v

    for v in "$@"; do
        [[ "$v" = "${kernel_major}.${kernel_minor}" ]] && return 0
    done

    return 1
}

# ---------------------------------------------------------------------------- #

function compile_c_to()
{
    local source

    source="$( mktemp )" &&
    set +o xtrace &&
    printf '%s\n' "${!1}" > "${source}" &&
    set -o xtrace &&
    "${CC:-cc}" \
        -std=c99 -Werror -Wall -Wextra -Wno-unused-parameter -pedantic \
        -O2 -x c "${source}" -lbdus -o "$2" \
        1>&2 &&
    rm -f "${source}" 1>&2 || {
        rm -f "${source}" 1>&2 &&
        false
    }
}

function compile_c()
{
    local binary

    binary="$( mktemp )" &&
    compile_c_to "$1" "${binary}" &&
    echo "${binary}" || {
        rm -f "${binary}" 1>&2 &&
        false
    }
}

function run_c()
{
    local run_c__driver

    run_c__driver="$( compile_c "$1" )" &&
    "${run_c__driver}" "${@:2}" &&
    rm -f "${run_c__driver}" 1>&2 || {
        rm -f "${run_c__driver}" 1>&2 &&
        false
    }
}

# ---------------------------------------------------------------------------- #

function compile_driver_inert()
{
    set +o xtrace &&
    compile_driver_inert__driver="$(< "${repo_root}/tests/shared/inert.c")" &&
    set -o xtrace &&
    compile_c compile_driver_inert__driver
}

function compile_driver_loop()
{
    set +o xtrace &&
    compile_driver_loop__driver="$(< "${repo_root}/tests/shared/loop.c")" &&
    set -o xtrace &&
    compile_c compile_driver_loop__driver
}

function compile_driver_ram()
{
    set +o xtrace &&
    compile_driver_ram__driver="$(< "${repo_root}/tests/shared/ram.c")" &&
    set -o xtrace &&
    compile_c compile_driver_ram__driver
}

function compile_verify_ioctl()
{
    set +o xtrace &&
    compile_verify_ioctl__driver="$(< "${repo_root}/tests/shared/verify-ioctl.c")" &&
    set -o xtrace &&
    compile_c compile_verify_ioctl__driver
}

function run_driver_inert()
{
    set +o xtrace &&
    run_driver_inert__driver="$(< "${repo_root}/tests/shared/inert.c")" &&
    set -o xtrace &&
    run_c run_driver_inert__driver "$@"
}

function run_driver_loop()
{
    set +o xtrace &&
    run_driver_loop__driver="$(< "${repo_root}/tests/shared/loop.c")" &&
    set -o xtrace &&
    run_c run_driver_loop__driver "$@"
}

function run_driver_ram()
{
    set +o xtrace &&
    run_driver_ram__driver="$(< "${repo_root}/tests/shared/ram.c")" &&
    set -o xtrace &&
    run_c run_driver_ram__driver "$@"
}

function run_verify_ioctl()
{
    set +o xtrace &&
    run_verify_ioctl__driver="$(< "${repo_root}/tests/shared/verify-ioctl.c")" &&
    set -o xtrace &&
    run_c run_verify_ioctl__driver "$@"
}

# ---------------------------------------------------------------------------- #
