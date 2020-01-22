FROM debian:10

# Separate layer because this is required no matter what
RUN apt-get update && apt-get install -y --no-install-recommends build-essential \
        && rm -rf /var/lib/apt/lists/*

# All the rest
RUN apt-get update && apt-get install -y --no-install-recommends \
                asciidoc \
                bash \
                build-essential \
                ccache \
                clang \
                cmake \
                elfutils \
                gcc-multilib \
                libzstd-dev \
                xsltproc \
        && rm -rf /var/lib/apt/lists/*

# redirect all compilers to ccache
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t;  done
