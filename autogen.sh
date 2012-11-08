#!/bin/sh

set -e

rm -f dev_mode_disabled
autoheader
autoconf
echo "Now run ./configure and make"
