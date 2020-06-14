#!/bin/sh -e

if [ -n "${VERBOSE}" ]; then
  set -x
fi

find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -exec misc/run-clang-format {} \;

echo "Formatting complete"
