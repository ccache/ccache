FROM debian:bullseye

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        bash \
        build-essential \
        ccache \
        clang \
        cmake \
        elfutils \
        gcc-multilib \
        libhiredis-dev \
        libzstd-dev \
        python3 \
        redis-server \
        redis-tools \
 && rm -rf /var/lib/apt/lists/*

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
