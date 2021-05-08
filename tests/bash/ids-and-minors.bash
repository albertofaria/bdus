# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test creates and destroys several devices and checks that their numerical
# identifiers and minor numbers are as expected.

# ---------------------------------------------------------------------------- #

max_devices=4089

# compile driver

driver_binary="$( compile_driver_inert )"
trap '{ rm -f "${driver_binary}"; }' EXIT

# lower max devices

modprobe --remove kbdus
modprobe kbdus max_devices="${max_devices}"

# define utility functions

function create()
{
    local path id minor_hex minor

    path="$( "${driver_binary}" )"
    id="${path:10}"
    minor_hex="$( stat "${path}" --format=%T )"
    minor="$(( 0x${minor_hex} ))"

    (( id == $1 ))
    (( minor == $2 ))
}

function destroy()
{
    bdus destroy --no-flush --quiet "$1"
}

function create_destroy_range()
{
    seq "$1" "$2" | xargs -P 0 -n 1 -- "${driver_binary}" > /dev/null
    seq "$1" "$2" | xargs -n 1 -- bdus destroy --no-flush --quiet
}

# create and destroy devices

create 0 $(( 0 * 256 ))
create 1 $(( 1 * 256 ))

destroy 1

create 2 $(( 2 * 256 ))
create 3 $(( 3 * 256 ))
create 4 $(( 4 * 256 ))

destroy 2

create 5 $(( 5 * 256 ))
create 6 $(( 6 * 256 ))

destroy 5

create_destroy_range 7 1000
create_destroy_range 1001 2000
create_destroy_range 2001 3000
create_destroy_range 3001 4088

create 4089 $(( 4089 * 256 ))
create 4090 $(( 4090 * 256 ))

destroy 4090

create 4091 $(( 4091 * 256 ))
create 4092 $(( 4092 * 256 ))
create 4093 $(( 4093 * 256 ))
create 4094 $(( 4094 * 256 ))
create 4095 $(( 4095 * 256 ))

create 4096 $(( 1 * 256 ))
create 4097 $(( 2 * 256 ))
create 4098 $(( 5 * 256 ))

# ---------------------------------------------------------------------------- #
