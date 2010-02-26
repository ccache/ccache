#!/bin/sh

set -e

autoheader
autoconf
echo "Now run ./configure-dev and make"
