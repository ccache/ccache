#!/bin/sh -ex

# While it's obviously quite impossible to support and test every single distribution,
# this script enables easy checking of the most common standard distributions at least.

# Runtime is roughly 1 minute per line (depending on system).
# First run takes about 1 additional minute per docker image (depending on internet connection).

# TODO: store docker images in (public) repository.
# TODO: use ccache within docker and preserve the cache.
#       That would make this script really fast and usable for small iterations!

echo "Warning: Docker support is rather experimental\n"

CC=gcc   CXX=g++     ./docker.sh debian-9-stretch
CC=clang CXX=clang++ ./docker.sh debian-9-stretch

CC=gcc   CXX=g++     ./docker.sh debian-10-buster
CC=clang CXX=clang++ ./docker.sh debian-10-buster

CC=gcc   CXX=g++     ./docker.sh ubuntu-16-xenial
CC=clang CXX=clang++ ./docker.sh ubuntu-16-xenial
