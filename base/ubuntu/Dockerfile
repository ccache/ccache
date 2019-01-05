FROM ubuntu:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
                gcc \
                make \
                bash \
                asciidoc xsltproc docbook-xml docbook-xsl \
                autoconf \
                gperf \
                zlib1g-dev \
        && rm -rf /var/lib/apt/lists/*
