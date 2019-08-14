FROM ubuntu:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
                gcc-multilib \
                make \
                bash \
                asciidoc xsltproc docbook-xml docbook-xsl \
                autoconf \
                libzstd-dev \
        && rm -rf /var/lib/apt/lists/*
