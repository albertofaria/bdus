# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test performs write operations on a file in an XFS file system residing
# in a recoverable device, interspersed with read operations that check if its
# contents are as expected and interspersed with ioctl commands, and
# concurrently and continuously replaces the driver, each time with a 50% chance
# of first killing the existing driver.

# ---------------------------------------------------------------------------- #

# compile loop driver and ioctl submission program

loop_driver="$( compile_driver_loop )"
trap '{ rm -f "${loop_driver}"; }' EXIT

verify_ioctl="$( compile_verify_ioctl )"
trap '{ rm -f "${loop_driver}" "${verify_ioctl}"; }' EXIT

# create devices

ram_device_path="$( run_driver_ram )"
loop_device_path="$( "${loop_driver}" "${ram_device_path}" )"

# create and mount file system

mkfs.xfs "${loop_device_path}"

mntp="$( mktemp -d )"
trap '{ rmdir "${mntp}"; rm -f "${loop_driver}"; }' EXIT

mount "${loop_device_path}" "${mntp}"
trap '{ umount "${mntp}"; rmdir "${mntp}"; rm -f "${loop_driver}"; }' EXIT

# start subshell to continually replace driver

export SHELLOPTS

(
    while true; do
        sleep 2
        (( RANDOM % 2 )) || { pkill -SIGKILL -f "${loop_driver}" && sleep 1; }
        "${loop_driver}" "${ram_device_path}" "${loop_device_path}"
    done
) &

# start subshell to continually submit ioctl commands

(
    modes=( none read write read-write )

    set +o xtrace  # too verbose

    while true; do
        mode_index=$(( RANDOM % ${#modes[@]} ))
        "${verify_ioctl}" "${modes[mode_index]}" "${loop_device_path}"
    done
) &

# run fio

time fio - <<EOF &
[global]
directory=${mntp}
filename_format=\$jobnum
numjobs=16
size=48m
io_size=256m
blocksize=512
runtime=2M
group_reporting=1
exitall=1

[write-verify]
readwrite=randwrite
ioengine=libaio
iodepth=32
direct=1
verify=crc32c
verify_backlog=1024
verify_fatal=1
verify_state_save=0

[read]
readwrite=randread
ioengine=libaio
iodepth=32
direct=1
EOF

# wait for fio or subshell to end (fio should end first on success, and the two
# subshells always terminate in failure)

wait -n

# unmount file system

umount "${mntp}"
trap '{ rmdir "${mntp}"; rm -f "${loop_driver}"; }' EXIT

# destroy devices

bdus destroy --no-flush "${loop_device_path}"
bdus destroy --no-flush "${ram_device_path}"

# wait for subshells to terminate

wait

# ---------------------------------------------------------------------------- #
