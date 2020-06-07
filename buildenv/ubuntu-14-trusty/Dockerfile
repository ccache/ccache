FROM ubuntu:trusty

# Seperate layer because this is required no matter what
RUN apt-get update && apt-get install -y --no-install-recommends build-essential \
        && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
                asciidoc \
                bash \
                ccache \
                clang \
                curl \
                elfutils \
                gcc-multilib \
                wget \
                xsltproc \
        && rm -rf /var/lib/apt/lists/*

# redirect all compilers to ccache
RUN for t in gcc g++ cc c++ clang clang++; do ln -vs /usr/bin/ccache /usr/local/bin/$t;  done

# Preinstalled cmake it too old (2.8.12.2)
RUN curl -sSL https://cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz | sudo tar -xzC /opt
RUN cp -a /opt/cmake-3.5.2-Linux-x86_64/bin /usr/local
RUN cp -a /opt/cmake-3.5.2-Linux-x86_64/share /usr/local
