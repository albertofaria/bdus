#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

set -o errexit -o pipefail -o nounset

script_dir="$( readlink -e "$0" | xargs dirname )"
repo_root="$( readlink -e "${script_dir}/.." )"

kernel_major="$( uname -r | cut -d '.' -f1 )"
kernel_minor="$( uname -r | cut -d '.' -f2 )"

# check usage

if (( $# == 0 )); then
    >&2 echo "Usage: run.bash <tests...>"
    exit 2
fi

for test in "$@"; do

    test_basename="$( basename "${test}" )"
    test_extension="${test_basename#*.}"

    if [[ ! "${test_extension}" =~ bash|c ]]; then
        >&2 echo "Invalid test file extension '${test_extension}'."
        exit 2
    fi

done

# check permissions

(( EUID == 0 )) || { >&2 echo "Must be run as root."; exit 1; }

# make sure kbdus is not loaded

modprobe -r kbdus

# run tests

num_succeeded=0

temp_dir="$( mktemp -d )"

trap '{
    (( num_succeeded == $# )) && color=2 || color=1
    >&2 echo
    >&2 echo -e "\033[3${color}mSUMMARY:\033[0m Successfully ran ${num_succeeded} of $# tests."
    rm -fr "${temp_dir}"
    }' EXIT

for test in "$@"; do

    # clear kernel ring buffer

    dmesg --clear

    # run test

    test_dirname="$( dirname "${test}" )"
    test_basename="$( basename "${test}" )"

    date

    test_exit=0

    case "${test_basename#*.}" in

        bash)
            "${SHELL}" \
                -c 'set -o errexit -o pipefail -o nounset
                    readonly repo_root="$1"
                    readonly kernel_major="$2"
                    readonly kernel_minor="$3"
                    source "$4/shared/util.bash"
                    cd "$5"
                    set -o xtrace
                    source "$6"
                    ' \
                "${SHELL}" \
                "${repo_root}" \
                "${kernel_major}" \
                "${kernel_minor}" \
                "${script_dir}" \
                "${test_dirname}" \
                "${test_basename}" ||
            test_exit="$?"
            ;;

        c)
            "${CC:-cc}" \
                -std=c99 -Werror -Wall -Wextra -Wno-unused-parameter -pedantic \
                "${test}" -lbdus -o "${temp_dir}/test" &&
            "${temp_dir}/test" ||
            test_exit="$?"
            ;;

        *)
            false
            ;;

    esac

    # destroy all devices

    destroy_exit=0

    find \
        /dev -maxdepth 1 -type b -name 'bdus-?*' \
        -exec bdus destroy --no-flush --quiet {} ';' \
        || destroy_exit="$?"

    # unload kbdus

    sleep 1

    modprobe_exit=0
    modprobe -r kbdus || { sleep 5; modprobe -r kbdus; } || modprobe_exit="$?"

    date

    # check if kernel ring buffer contains bad things

    dmesg --color=always --decode --level=emerg,alert,crit,err,warn |&
        { grep -Fiv 'Buffer I/O error on dev bdus-' || true; } |
        { grep -Fiv 'callbacks suppressed' || true; } |
        { grep -Fiv 'error, dev bdus-' || true; } |
        { grep -Fiv 'interrupt took' || true; } |
        { grep -Fiv 'loading out-of-tree module taints kernel' || true; } |
        { grep -Fiv 'partition table beyond EOD, truncated' || true; } |
        { grep -Fiv 'run blktests' || true; } |
        { grep -Fiv 'unable to read RDB block 0' || true; } |
        { grep -Fiv 'xfs filesystem being mounted at' || true; } |
        tee /dev/stderr |
        (( $( wc -c ) == 0 ))

    # check if everything succeeded

    (( test_exit == 0 )) || {
        >&2 echo -e "\033[31mTest failed with exit code ${test_exit}.\033[0m"
        false
    }

    (( destroy_exit == 0 )) || {
        >&2 echo -e "\033[31mFailed to destroy devices after test.\033[0m"
        false
    }

    (( modprobe_exit == 0 )) || {
        >&2 echo -e "\033[31mFailed to unload kbdus.\033[0m"
        false
    }

    # test succeeded

    num_succeeded=$(( num_succeeded + 1 ))

done

# ---------------------------------------------------------------------------- #
