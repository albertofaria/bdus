# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

# This test ensures that the examples in the /examples directory compile
# successfully without warnings under both C99 and C11.

# ---------------------------------------------------------------------------- #

for example in "${repo_root}"/examples/*.c; do

    for std in c99 c11; do

        "${CC:-cc}" \
            -Werror -Wall -Wextra -Wno-unused-parameter -pedantic \
            -std="${std}" "${example}" -lbdus -o /dev/null

    done

done

# ---------------------------------------------------------------------------- #
