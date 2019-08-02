#!/bin/sh

set -e

if [ -f dev_mode_disabled ]; then
    cat <<EOF >&2
Error: It looks like you are building ccache from a release archive. If so,
there is no need to run autogen.sh. See INSTALL.md for further instructions.

If you do want to the enable the development mode, delete the file
dev_mode_disabled first, but it's probably a better idea to work with the
proper ccache Git repository directly as described on
<https://ccache.dev/repo.html>.
EOF
    exit 1
fi

autoheader
autoconf
echo "Now run ./configure and make"
