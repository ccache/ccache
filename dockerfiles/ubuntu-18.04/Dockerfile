FROM ubuntu:18.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        asciidoctor \
        bash \
        build-essential \
        ccache \
        clang \
        docbook-xml \
        docbook-xsl \
        elfutils \
        gcc-multilib \
        gpg \
        libhiredis-dev \
        libzstd-dev \
        python3 \
        redis-server \
        redis-tools \
        software-properties-common \
        wget \
 && add-apt-repository ppa:ubuntu-toolchain-r/test \
 && apt install -y --no-install-recommends g++-9 \
 && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - >/usr/share/keyrings/kitware-archive-keyring.gpg \
 && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ bionic main' >/etc/apt/sources.list.d/kitware.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends cmake \
 && rm -rf /var/lib/apt/lists/*

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
