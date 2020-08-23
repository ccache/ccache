FROM alpine:3.12

RUN apk add --no-cache \
        bash \
        ccache \
        clang \
        cmake \
        elfutils \
        g++ \
        gcc \
        libc-dev \
        make \
        perl \
        zstd-dev

# Redirect all compilers to ccache.
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t; done
