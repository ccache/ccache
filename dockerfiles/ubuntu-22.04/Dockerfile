FROM ubuntu:22.04

# Non-interactive: do not set up timezone settings.
RUN apt-get update \
 && DEBIAN_FRONTEND="noninteractive" apt-get install -y --no-install-recommends \
        asciidoctor \
        bash \
        build-essential \
        ccache \
        clang \
        cmake \
        docbook-xml \
        docbook-xsl \
        elfutils \
        gcc-multilib \
        gcc-12 \
        g++-12 \
        libhiredis-dev \
        libzstd-dev \
        python3 \
        redis-server \
        redis-tools \
 && rm -rf /var/lib/apt/lists/*

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
