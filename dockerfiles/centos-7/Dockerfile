FROM centos:7

RUN yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
 && yum install -y centos-release-scl \
 && yum install -y \
        autoconf \
        bash \
        ccache \
        clang \
        cmake3 \
        devtoolset-11 \
        elfutils \
        gcc \
        gcc-c++ \
        libzstd-devel \
        make \
        python3 \
 && yum autoremove -y \
 && yum clean all \
 && cp /usr/bin/cmake3 /usr/bin/cmake \
 && cp /usr/bin/ctest3 /usr/bin/ctest

ENTRYPOINT ["scl", "enable", "devtoolset-11", "--"]
