FROM debian:10

RUN apt-get update && apt-get install -y --no-install-recommends \
                asciidoc \
                bash \
                build-essential \
                clang \
                cmake \
                elfutils \
                gcc-multilib \
                libb2-dev \
                libzstd-dev \
                xsltproc \
        && rm -rf /var/lib/apt/lists/*
