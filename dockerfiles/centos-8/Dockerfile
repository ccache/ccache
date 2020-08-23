FROM centos:8

RUN dnf install -y epel-release \
 && dnf install -y \
        asciidoc \
        autoconf \
        bash \
        ccache \
        clang \
        cmake \
        diffutils \
        elfutils \
        gcc \
        gcc-c++ \
        libzstd-devel \
        make \
 && dnf clean all
