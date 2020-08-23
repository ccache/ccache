FROM ubuntu:16.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        asciidoc \
        bash \
        build-essential \
        ccache \
        clang \
        cmake \
        elfutils \
        gcc-multilib \
        libzstd1-dev \
        xsltproc \
 && rm -rf /var/lib/apt/lists/*

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
