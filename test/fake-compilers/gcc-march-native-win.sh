#!/bin/bash

args=("$@")

if [ "$1" = "-###" ]; then
    cat <<EOF >&2
Using built-in specs.
...
COLLECT_GCC_OPTIONS='-E' ...
 "/example/cc1.exe" -E -quiet $CC1_ARGS
COMPILER_PATH=/example
EOF
    echo "bin/cc1"
elif [ "$1" = "-E" ]; then
    echo preprocessed >"${args[$#-2]}"
else
    echo compiled >"${args[$#-2]}"
fi
