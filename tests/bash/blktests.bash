# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test runs several tests from the blktests suite
# (https://github.com/osandov/blktests) on a RAM device.

# ---------------------------------------------------------------------------- #

# create temporary directory

temp_dir="$( mktemp -d )"

trap '{ rm -fr "${temp_dir}"; }' EXIT

# get blktests

curl -LsS https://github.com/osandov/blktests/archive/master.tar.gz |
    tar -C "${temp_dir}" -xzf -

make -C "${temp_dir}/blktests-master"

# create device

device_path="$( run_driver_ram )"

# run blktests

cd "${temp_dir}/blktests-master"

TEST_DEVS="${device_path}" DEVICE_ONLY=1 EXCLUDE=block/008 ./check

# ---------------------------------------------------------------------------- #
