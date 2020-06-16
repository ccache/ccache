#!/bin/sh -ex

# While it's obviously quite impossible to support and test every single distribution,
# this script enables easy checking of the most common standard distributions at least.

# Runtime is roughly 1 minute per line (depending on system).
# First run takes about 1 additional minute per docker image (depending on internet connection).

# Note: Currently this is more a reference on how to run each instance,
#       instead of running this entire script at once. See next steps.

# Next steps:
# * run compilation, tests and/or docker instances in parallel to improve runtime.
# * improve detection of failures so this script can be executed as a whole (see Note in alpine 3.4 Dockerfile).

echo "Warning: Docker support is rather experimental\n"


# Debian

  # See https://github.com/ccache/ccache/issues/602
  #CC=gcc   CXX=g++     ./docker.sh debian-9-stretch
  #CC=clang CXX=clang++ ./docker.sh debian-9-stretch

  CC=gcc   CXX=g++     ./docker.sh debian-10-buster
  CC=clang CXX=clang++ ./docker.sh debian-10-buster


# Ubuntu (ancient, old and latest)

  # zstd and libb2 not available for Ubuntu 14.
  CC=gcc   CXX=g++     CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON -DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh ubuntu-14-trusty

  # See https://github.com/ccache/ccache/issues/601
  #CC=clang CXX=clang++ CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON -DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh ubuntu-14-tusty

  CC=gcc   CXX=g++     ./docker.sh ubuntu-16-xenial
  CC=clang CXX=clang++ ./docker.sh ubuntu-16-xenial

  CC=gcc   CXX=g++     CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON" ./docker.sh ubuntu-20-focal
  CC=clang CXX=clang++ CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON" ./docker.sh ubuntu-20-focal


# Alpine (old and latest)

  CC=gcc   CXX=g++     CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON -DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh alpine-3.4
  # Clang is not capable to compile libzstd from internet before alpine 3.12 (Some SSE2 error regarding missing file emmintrin.h)
  #CC=clang CXX=clang++ CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON -DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh alpine-3.4
  CC=clang CXX=clang++ CMAKE_PARAMS="-DUSE_LIBZSTD_FROM_INTERNET=ON -DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh alpine-3.12
  CC=gcc   CXX=g++     CMAKE_PARAMS="-DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh alpine-3.12
  CC=clang CXX=clang++ CMAKE_PARAMS="-DUSE_LIBB2_FROM_INTERNET=ON" ./docker.sh alpine-3.12
