#!/bin/sh -ex
# doc/INSTALL.md
./autogen.sh
./configure
make
make ${*:-test}
