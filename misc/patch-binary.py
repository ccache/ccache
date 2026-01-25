#!/usr/bin/env python3

import sys

libexecdir = sys.argv[1]
sysconfdir = sys.argv[2]

replacements = [
    (b"/usr/local/libexec", libexecdir.encode()),
    (b"/usr/local/etc", sysconfdir.encode()),
]
ccache_bin = sys.stdin.buffer.read()
for repl_from, repl_to in replacements:
    ccache_bin = ccache_bin.replace(
        repl_from + b"\x00" * (4096 - len(repl_from)),
        repl_to + b"\x00" * (4096 - len(repl_to)),
    )
sys.stdout.buffer.write(ccache_bin)
