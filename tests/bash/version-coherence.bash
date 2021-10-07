# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that occurrences of version numbers in the repository match
# the current version.

# ---------------------------------------------------------------------------- #

# ensure that kbdus.h and bdus.h advertise the current version

cd "${repo_root}"

function get_header_version()
{
    local major minor patch
    major="$( grep -Po '#define K?BDUS_HEADER_VERSION_MAJOR \K\d+' "$1" )"
    minor="$( grep -Po '#define K?BDUS_HEADER_VERSION_MINOR \K\d+' "$1" )"
    patch="$( grep -Po '#define K?BDUS_HEADER_VERSION_PATCH \K\d+' "$1" )"
    echo "${major}.${minor}.${patch}"
}

kbdus_version="$( get_header_version kbdus/include/kbdus.h )"
libbdus_version="$( get_header_version libbdus/include/bdus.h )"

[[ "${kbdus_version}" = "${libbdus_version}" ]]

# ensure that the README and all code only mention the current version

pattern='(?!'"${libbdus_version}"')(?<!Linux )\d+\.\d+\.\d+'

grep_exit=0
grep -PR "${pattern}" README.md cmdbdus libbdus kbdus || grep_exit="$?"

(( grep_exit == 1 ))

# ---------------------------------------------------------------------------- #
