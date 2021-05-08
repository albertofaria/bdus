#compdef bdus
# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

_bdus_has_word()
{
    (( ${words[(ie)$1]} < CURRENT ))
}

_bdus_has_subcmd_args()
{
    (( ${words[(I)[!-]*]} > 2 )) && (( ${words[(I)[!-]*]} < CURRENT ))
}

local candidates=()

if (( CURRENT < 2 )) || _bdus_has_word --help; then

    :

elif (( CURRENT == 2 )); then

    if [[ "${words[2]}" = -* ]]; then
        candidates+=( '--help' )
    else
        candidates+=(
            'destroy:destroy a device'
            'version:print version information'
            )
    fi

elif [[ "${words[2]}" = destroy ]]; then

    if [[ "${words[$CURRENT]}" = -* ]]; then

        (( CURRENT != 3 )) || candidates+=( '--help' )

        _bdus_has_word --no-flush ||
            candidates+=( "--no-flush:don't flush previously written data" )

        _bdus_has_word -q || _bdus_has_word --quiet ||
            candidates+=( "--quiet:print only error messages" )

    elif ! _bdus_has_subcmd_args; then

        candidates+=(
            ${(f)"$( find /dev -maxdepth 1 -type b -name "bdus-?*" | sort -V )"}
            )

    fi

elif [[ "${words[2]}" = version ]]; then

    if [[ "${words[$CURRENT]}" = -* ]]; then

        (( CURRENT != 3 )) || candidates+=( '--help' )

        _bdus_has_word --cmdbdus ||
            candidates+=( '--cmdbdus:print the version of this command' )

        _bdus_has_word --libbdus ||
            candidates+=( '--libbdus:print the version of libbdus in use' )

        _bdus_has_word --kbdus ||
            candidates+=( '--kbdus:print the version of kbdus' )

    fi

fi

_describe -V bdus candidates

# ---------------------------------------------------------------------------- #
