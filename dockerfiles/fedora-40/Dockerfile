ARG BASE_IMAGE=fedora:40
FROM ${BASE_IMAGE} AS build

RUN dnf install -y \
        blake3-devel \
        ccache \
        clang \
        cmake \
        cpp-httplib-devel \
        diffutils \
        doctest-devel \
        elfutils \
        expected-devel \
        findutils \
        fmt-devel \
        gcc \
        gcc-c++ \
        libstdc++-static \
        hiredis-devel \
        less \
        libzstd-devel \
        ninja-build \
        perl \
        python3 \
        redis \
        xxhash-devel \
 && dnf clean all
