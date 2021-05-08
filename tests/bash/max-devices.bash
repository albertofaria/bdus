# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that the max_devices kbdus parameter can be set and that
# that number of devices, and no more, can exist simultaneously.

# ---------------------------------------------------------------------------- #

max_devices=1234

# lower max devices

modprobe --remove kbdus
modprobe kbdus max_devices="${max_devices}"

# compile driver

driver_binary="$( compile_driver_inert )"

trap '{ rm -f "${driver_binary}"; }' EXIT

# create devices

seq 0 $(( max_devices - 1 )) | xargs -P 0 -n 1 -- \
    bash -c 'p="$( "$1" )" && (( ${p:10} < $2 ))' \
    bash "${driver_binary}" "${max_devices}"

# create device expecting failure

! "${driver_binary}"

# destroy two devices

bdus destroy --no-flush --quiet 42
bdus destroy --no-flush --quiet 1000

# create two devices

"${driver_binary}"
"${driver_binary}"

# create device expecting failure again

! "${driver_binary}"

# ---------------------------------------------------------------------------- #
