# NOTE: This is not the real Docker image used for the Travis builds.
#       See: https://docs.travis-ci.com/user/common-build-problems/

FROM ubuntu:xenial

# https://github.com/Yelp/dumb-init
ADD https://github.com/Yelp/dumb-init/releases/download/v1.2.2/dumb-init_1.2.2_amd64.deb .
RUN dpkg -i dumb-init_*.deb
ENTRYPOINT ["/usr/bin/dumb-init", "--"]

# generic tools
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                libc6-dev \
                gcc \
                build-essential \
                clang \
                cmake \
                libc6-dev-i386 \
                gcc-multilib \
                gcc-mingw-w64 \
                make \
                autoconf \
        && rm -rf /var/lib/apt/lists/*

# Travis has upgraded clang, from clang-3.8 to clang-7.0
RUN printf "deb https://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main\ndeb-src https://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main\n" > /etc/apt/sources.list.d/llvm-toolchain.list && apt-key adv --fetch-keys https://apt.llvm.org/llvm-snapshot.gpg.key && apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 1E9377A2BA9EF27F

RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                clang-7 \
        && rm -rf /var/lib/apt/lists/* \
        && ln -s /usr/bin/clang-7 /usr/local/bin/clang

# ccache specific
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                elfutils \
                libzstd1-dev \
                libb2-dev \
                wget \
                ca-certificates \
        && rm -rf /var/lib/apt/lists/*
