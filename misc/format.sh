#!/bin/sh -ex
find src unittest -path src/third_party -prune -o -regex ".*\.[ch]p?p?" -exec misc/run-clang-format {} \;
