#!/bin/sh -e

if [ -n "${VERBOSE}" ]; then
  set -x
fi

find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -exec misc/run-clang-format {} \;

if hash cmake-format 2>/dev/null; then
  # Top level CMakeLists.txt + subidrectories.
  # This avoids running the check on any build directories.
  cmake-format -i CMakeLists.txt
  find cmake -name "*.cmake" -exec cmake-format -i {} \;
  find src unittest -name "CMakeLists.txt" -exec cmake-format -i {} \;
else
  echo "Note: cmake-format not installed. CMake files will not be formatted."
  echo "You can install it via pip3 install cmake-format"
fi

echo "Formating complete"
