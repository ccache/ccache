# NOTE: This is not the real Docker image used for the Travis builds.
#       See: https://docs.travis-ci.com/user/common-build-problems/

FROM ubuntu:trusty

# https://github.com/Yelp/dumb-init
ADD https://github.com/Yelp/dumb-init/releases/download/v1.2.1/dumb-init_1.2.1_amd64.deb .
RUN dpkg -i dumb-init_*.deb
ENTRYPOINT ["/usr/bin/dumb-init", "--"]

# generic tools
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                libc6-dev \
                gcc \
                clang \
                gcc-mingw-w64 \
                make \
                autoconf \
        && rm -rf /var/lib/apt/lists/*

# ccache specific
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                gperf \
                elfutils \
                zlib1g-dev \
        && rm -rf /var/lib/apt/lists/*
