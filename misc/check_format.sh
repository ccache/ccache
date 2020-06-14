#!/bin/sh -e

if [ -n "${VERBOSE}" ]; then
  set -x
fi

# xarg returns 1 if any run-clang-format call returns 1.
clang-format --version
find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -print0 | xargs -0 -n1 misc/run-clang-format --check

echo "Format is ok"
