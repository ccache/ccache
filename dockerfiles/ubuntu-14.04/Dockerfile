FROM ubuntu:14.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        asciidoc \
        bash \
        build-essential \
        ccache \
        clang-3.4 \
        curl \
        docbook-xml \
        docbook-xsl \
        elfutils \
        g++-multilib \
        ninja-build \
        wget \
        xsltproc \
 && rm -rf /var/lib/apt/lists/*

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done

# The distribution's CMake it too old (2.8.12.2).
RUN curl -sSL https://cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz | sudo tar -xzC /opt \
 && cp -a /opt/cmake-3.5.2-Linux-x86_64/bin /usr/local \
 && cp -a /opt/cmake-3.5.2-Linux-x86_64/share /usr/local
