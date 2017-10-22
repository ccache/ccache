FROM ubuntu:16.04

RUN apt-get update && apt-get install -y --no-install-recommends \
                gcc \
                make \
                autoconf \
                gperf \
                zlib1g-dev \
                libmemcached-dev \
        && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/build

COPY . .

RUN ./autogen.sh \
        && ./configure \
        && make \
        && make test
