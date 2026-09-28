ARG BASE_IMAGE=fedora:42
FROM ${BASE_IMAGE} AS build

RUN dnf install -y \
        awk \
        blake3-devel \
        ccache \
        clang \
        cmake \
        diffutils \
        doctest-devel \
        elfutils \
        expected-devel \
        findutils \
        fmt-devel \
        gcc \
        gcc-c++ \
        libstdc++-static \
        less \
        libzstd-devel \
        ninja-build \
        perl \
        python3 \
        xxhash-devel \
 && dnf clean all
