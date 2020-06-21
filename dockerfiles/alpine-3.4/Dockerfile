# Released 2016, this is the first release to contain cmake >= 3.4.3

FROM alpine:3.4

RUN apk add --no-cache \
                asciidoc \
                bash \
                ccache \
                # clang \
                cmake \
                # elfutils \ (available since Alpine 3.7)
                g++ \
                gcc \
                libc-dev \
                make \
                perl

# redirect all compilers to ccache
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t;  done
