# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that all C code is properly formatted.

# ---------------------------------------------------------------------------- #

# create temporary directory

temp_dir="$( mktemp -d )"
trap '{ rm -fr "${temp_dir}"; }' EXIT

# create virtualenv

virtualenv -p python3 "${temp_dir}"

set +o nounset
source "${temp_dir}/bin/activate"
set -o nounset

# install clang-format

pip install clang-format

# check formatting

cd "${repo_root}"

find . -name '*.[ch]' -print0 |
    xargs -0 clang-format --style=file --dry-run --Werror

# ---------------------------------------------------------------------------- #
