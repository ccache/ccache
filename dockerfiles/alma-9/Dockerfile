ARG BASE_IMAGE=almalinux:9
FROM ${BASE_IMAGE} AS build

RUN dnf install -y dnf-plugins-core \
 && dnf config-manager --set-enabled crb \
 && dnf install -y epel-release \
 && dnf install -y \
        blake3-devel \
        ccache \
        clang \
        cmake \
        diffutils \
        doctest-devel \
        elfutils \
        expected-devel \
        fmt-devel \
        gcc \
        gcc-c++ \
        less \
        libstdc++-devel \
        libstdc++-static \
        libzstd-devel \
        make \
        ninja-build \
        perl \
        python3 \
        xxhash-devel \
 && dnf autoremove -y \
 && dnf clean all
