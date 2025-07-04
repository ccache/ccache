ARG BASE_IMAGE=quay.io/pypa/manylinux_2_28
FROM ${BASE_IMAGE} AS build

RUN dnf install -y dnf-plugins-core
RUN dnf config-manager --set-enabled powertools
RUN dnf install -y epel-release
RUN dnf install -y \
        clang \
        cmake \
        diffutils \
        elfutils \
        expected-devel \
        gcc \
        gcc-c++ \
        libstdc++-devel \
        libstdc++-static \
        less \
        libzstd-devel \
        make \
        ninja-build \
        perl \
        python3 \
        redis \
        xxhash-devel
RUN dnf autoremove -y
RUN dnf clean all
