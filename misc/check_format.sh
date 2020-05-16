#!/bin/sh -e

if [ -n "${VERBOSE}" ]; then
  set -x
fi

# xarg returns 1 if any run-clang-format call returns 1.
clang-format --version
find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -print0 | xargs -0 -n1 misc/run-clang-format --check

# Top level CMakeLists.txt + subidrectories.
# This avoids running the check on any build directories.
if hash cmake-format 2>/dev/null; then
  printf "cmake-format version "
  cmake-format --version
  CLANG_FORMAT=cmake-format misc/run-clang-format --check CMakeLists.txt
  find cmake -name "*.cmake" -print0 | CLANG_FORMAT=cmake-format xargs -0 -n1 misc/run-clang-format --check
  find src unittest -name "CMakeLists.txt" -print0 | CLANG_FORMAT=cmake-format xargs -0 -n1 misc/run-clang-format --check
else
  echo "Note: cmake-format not installed. CMake files will not be checked for correct formatting."
  echo "You can install it via pip3 install cmake-format"
fi

echo "Format is ok"
