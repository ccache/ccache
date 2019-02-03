#!/bin/sh -ex
make clean
make travis CC=gcc
make travis CC=clang
make travis CC=gcc CFLAGS="-m32 -g -O2" HOST="--host=i386-linux-gnu"
make travis CC=i686-w64-mingw32-gcc HOST="--host=i686-w64-mingw32" TEST="unittest/run.exe"
make travis CC=clang CFLAGS="-fsanitize=undefined" ASAN_OPTIONS="detect_leaks=0"
make travis CC=clang CFLAGS="-fsanitize=address -g" ASAN_OPTIONS="detect_leaks=0"
make travis CC=/usr/bin/clang TEST=analyze
