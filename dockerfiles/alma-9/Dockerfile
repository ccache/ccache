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
        cpp-httplib-devel \
        diffutils \
        doctest-devel \
        elfutils \
        expected-devel \
        fmt-devel \
        gcc \
        gcc-c++ \
        hiredis-devel \
        less \
        libzstd-devel \
        make \
        ninja-build \
        perl \
        python3 \
        redis \
        xxhash-devel \
 && dnf autoremove -y \
 && dnf clean all
