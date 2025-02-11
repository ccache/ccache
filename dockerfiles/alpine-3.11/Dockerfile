ARG BASE_IMAGE=alpine:3.11
FROM ${BASE_IMAGE} AS build

RUN apk add --no-cache \
        bash \
        ccache \
        clang \
        cmake \
        elfutils \
        g++ \
        gcc \
        hiredis-dev \
        libc-dev \
        make \
        perl \
        python3 \
        redis \
        zstd-dev

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
