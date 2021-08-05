# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that devices are initially configured with the "none"
# scheduler, and that the scheduler can be switched after device creation.

# ---------------------------------------------------------------------------- #

# create device

device_path="$( run_driver_ram )"
scheduler_file="/sys/block/$( basename "${device_path}" )/queue/scheduler"

# ensure that initial scheduler is "none"

grep '\[none\]' "${scheduler_file}"

# ensure that scheduler can be switched

echo mq-deadline > "${scheduler_file}"
grep '\[mq-deadline\]' "${scheduler_file}"

# ---------------------------------------------------------------------------- #
