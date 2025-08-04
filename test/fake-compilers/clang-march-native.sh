#!/bin/bash

args=("$@")

if [ "$1" = "-###" ]; then
    cat <<EOF >&2
...
InstalledDir: /usr/bin
 (in-process)
 "/example/bin/clang" "-cc1" $CC1_ARGS
EOF
    echo "bin/cc1"
elif [ "$1" = "-E" ]; then
    echo preprocessed >"${args[$#-2]}"
else
    echo compiled >"${args[$#-2]}"
fi
