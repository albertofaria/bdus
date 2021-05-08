# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

_bdus_has_word()
{
    for word in "${COMP_WORDS[@]::${#COMP_WORDS[@]} - 1}"; do
        [[ "${word}" != "$1" ]] || return 0
    done

    return 1
}

_bdus_has_subcmd_args()
{
    for word in "${COMP_WORDS[@]:2:${#COMP_WORDS[@]} - 3}"; do
        [[ "${word}" = -* ]] || return 0
    done

    return 1
}

_bdus_completion()
{
    local candidates=

    if (( ${#COMP_WORDS[@]} < 2 )) || _bdus_has_word --help; then

        :

    elif (( ${#COMP_WORDS[@]} == 2 )); then

        if [[ "${COMP_WORDS[1]}" = -* ]]; then
            candidates=--help
        else
            candidates="destroy version"
        fi

    elif [[ "${COMP_WORDS[1]}" = destroy ]]; then

        if [[ "${COMP_WORDS[COMP_CWORD]}" = -* ]]; then

            (( ${#COMP_WORDS[@]} != 3 )) || candidates+=" --help"

            _bdus_has_word --no-flush ||
                candidates+=" --no-flush"

            _bdus_has_word -q || _bdus_has_word --quiet ||
                candidates+=" --quiet"

        elif ! _bdus_has_subcmd_args; then

            candidates="$( find /dev -maxdepth 1 -type b -name "bdus-?*" | sort -V )"

        fi

    elif [[ "${COMP_WORDS[1]}" = version ]]; then

        if [[ "${COMP_WORDS[COMP_CWORD]}" = -* ]]; then

            (( ${#COMP_WORDS[@]} != 3 )) || candidates+=" --help"

            _bdus_has_word --cmdbdus || candidates+=" --cmdbdus"
            _bdus_has_word --libbdus || candidates+=" --libbdus"
            _bdus_has_word --kbdus   || candidates+=" --kbdus"

        fi

    fi

    COMPREPLY=( $( compgen -W "${candidates}" -- "${COMP_WORDS[COMP_CWORD]}" ) )
}

complete -o nosort -F _bdus_completion bdus

# ---------------------------------------------------------------------------- #
