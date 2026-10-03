ARG BASE_IMAGE=alpine:3.23
FROM ${BASE_IMAGE} AS build

RUN apk add --no-cache \
        bash \
        ccache \
        clang \
        cmake \
        doctest-dev \
        elfutils \
        g++ \
        gcc \
        make \
        perl \
        python3 \
        xxhash-dev \
        zstd-dev

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
