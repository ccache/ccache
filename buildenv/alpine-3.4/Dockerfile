# Released 2016, this is the first release to contain cmake >= 3.4.3

# Note: While alpine 3.4 runs fine, cmake 3.8.1 reports skipped tests as failures while cmake 3.9.5 reports them as skipped.
# So alpine 3.7 is the first version to report exit code 0. Maybe install new cmake manually here?

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
