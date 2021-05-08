# SPDX-License-Identifier: MIT
# ---------------------------------------------------------------------------- #

function __bdus_args

    set -l tokens (commandline -cop)

    if not count $argv > /dev/null
        for t in $tokens
            echo -- $t
        end
    else if [ $argv[1] -ge 0 ]
        echo -- $tokens[$argv[1]]
    else
        for t in $tokens[(math 1 - $argv[1])..-1]
            echo -- $t
        end
    end

end

complete \
    -c bdus \
    -f

complete \
    -c bdus \
    -n 'contains -- (count (__bdus_args)) 1 2' \
    -l help

# ---------------------------------------------------------------------------- #
# subcommand "destroy"

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (count (__bdus_args)) = 1 ]' \
    -a 'destroy' \
    -d 'Destroy a device'

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = destroy ]
        and not string match -r "^[^-].*\$" -- (__bdus_args -2) > /dev/null' \
    -a '( find /dev -maxdepth 1 -type b -name "bdus-?*" | sort -V )'

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = destroy ]
        and not contains -- --no-flush (__bdus_args)' \
    -l no-flush \
    -d "Don't flush previously written data"

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = destroy ]
        and not contains -- -q (__bdus_args)
        and not contains -- --quiet (__bdus_args)' \
    -l quiet \
    -d 'Print only error messages'

# ---------------------------------------------------------------------------- #
# subcommand "version"

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (count (__bdus_args)) = 1 ]' \
    -a 'version' \
    -d 'Print version information'

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = version ]
        and not contains -- --cmdbdus (__bdus_args)' \
    -l cmdbdus \
    -d 'Print the version of this command'

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = version ]
        and not contains -- --libbdus (__bdus_args)' \
    -l libbdus \
    -d 'Print the version of libbdus in use'

complete \
    -c bdus \
    -n 'not contains -- --help (__bdus_args)
        and [ (__bdus_args 2) = version ]
        and not contains -- --kbdus (__bdus_args)' \
    -l kbdus \
    -d 'Print the version of kbdus'

# ---------------------------------------------------------------------------- #
