#!/bin/bash

# Reformat the given C++ file if required, or if --check
# is specified, only print the diff.
# Exits with 0 if the file was already formatted correctly.

set -eu

clang_format="${CLANG_FORMAT:-clang-format}"
cf_diff_color="${cf_diff_color:-}"

if [[ "${1:-}" == "--check" ]]; then
    shift
    check=true
else
    check=false
fi

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 [--check] file.cpp"
    exit 1
fi

file="$1"

if [[ ! -e "$file" ]]; then
    echo "No such file: $file"
    exit 1
fi

tmp_file="$file.$$.clang-format.tmp"
trap "rm -f \"$tmp_file\"" EXIT

"$clang_format" "$file" >"$tmp_file"

if ! cmp -s "$file" "$tmp_file"; then
    if $check; then
        git diff $cf_diff_color --no-index "$file" "$tmp_file" |
            sed -r -e "s!^---.*!--- a/$file!" -e "s!^\+\+\+.*!+++ b/$file!" \
            -e "/diff --/d" -e "/index /d" -e "s/.[0-9]*.clang-format.tmp//"
    else
        echo "Reformatted $file"
        mv "$tmp_file" "$file" && trap '' EXIT
    fi
    exit 1
fi
