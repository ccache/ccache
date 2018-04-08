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
                libc6-dev-i386 \
                gcc-multilib \
                gcc-mingw-w64 \
                make \
                autoconf \
        && rm -rf /var/lib/apt/lists/*

# Travis has upgraded clang, from clang-3.4 to clang-5.0
# https://github.com/travis-ci/travis-cookbooks/pull/890
RUN printf "deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-5.0 main\ndeb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-5.0 main\n# Also add the following for the appropriate libstdc++\ndeb http://ppa.launchpad.net/ubuntu-toolchain-r/test/ubuntu trusty main\n" > /etc/apt/sources.list.d/llvm-toolchain.list && apt-key adv --fetch-keys http://apt.llvm.org/llvm-snapshot.gpg.key && apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 1E9377A2BA9EF27F
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                clang-5.0 \
        && rm -rf /var/lib/apt/lists/* \
        && ln -s /usr/bin/clang-5.0 /usr/local/bin/clang

# ccache specific
RUN apt-get -qq update && apt-get install -y --no-install-recommends \
                gperf \
                elfutils \
                zlib1g-dev \
                lib32z1-dev \
        && rm -rf /var/lib/apt/lists/*
