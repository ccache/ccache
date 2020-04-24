#!/bin/sh -ex

# xarg returns 1 if any run-clang-format call returns 1.
find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -print0 | xargs -0 -n1 misc/run-clang-format --check

# Top level CMakeLists.txt + subidrectories.
# This avoids running the check on any build directories.
cmake-format --check CMakeLists.txt
find cmake -name "*.cmake" -print0 | xargs -0 -n1 cmake-format --check
find src unittest -name "CMakeLists.txt" -print0 | xargs -0 -n1 cmake-format --check
