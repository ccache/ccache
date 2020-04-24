#!/bin/sh -ex
find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -exec misc/run-clang-format {} \;

# Top level CMakeLists.txt + subidrectories.
# This avoids running the check on any build directories.
cmake-format -i CMakeLists.txt
find cmake -name "*.cmake" -exec cmake-format -i {} \;
find src unittest -name "CMakeLists.txt" -exec cmake-format -i {} \;
