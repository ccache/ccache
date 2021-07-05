FROM centos:8

# also run update due to https://bugs.centos.org/view.php?id=18212
RUN dnf install -y epel-release \
 && dnf update -y \
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
