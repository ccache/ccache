#!/bin/sh -ex

make clean
make travis CC=gcc CXX=g++
make travis CC=clang CXX=clang++
make travis CC=gcc CFLAGS="-m32 -g -O2" CXX=g++ CXXFLAGS="-m32 -g -O2" LDFLAGS="-m32" CONFIGURE="--host=i386-linux-gnu --with-libzstd-from-internet --with-libb2-from-internet"
make travis CC=i686-w64-mingw32-gcc-posix CXX=i686-w64-mingw32-g++-posix CONFIGURE="--host=i686-w64-mingw32 --with-libzstd-from-internet --with-libb2-from-internet" TEST="unittest/run.exe"
make travis CC=clang CXX=clang++ CFLAGS="-fsanitize=undefined" LDFLAGS="-fsanitize=undefined" ASAN_OPTIONS="detect_leaks=0"
make travis CC=clang CXX=clang++ CFLAGS="-fsanitize=address -g" LDFLAGS="-fsanitize=address" ASAN_OPTIONS="detect_leaks=0"
make travis CC=/usr/bin/clang CXX=/usr/bin/clang++ TEST=analyze
make travis CC=/usr/bin/clang CXX=/usr/bin/clang++ TEST=tidy
make travis CC=/usr/bin/clang CXX=/usr/bin/clang++ TEST=check_format
