FROM centos:8

# also run update due to https://bugs.centos.org/view.php?id=18212
RUN dnf install -y epel-release \
 && dnf update -y \
 && dnf install -y \
        asciidoctor \
        autoconf \
        bash \
        ccache \
        clang \
        cmake \
        diffutils \
        elfutils \
        gcc \
        gcc-c++ \
        hiredis-devel \
        libzstd-devel \
        make \
        python3 \
        redis \
 && dnf clean all
