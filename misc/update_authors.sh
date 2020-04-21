#!/bin/sh

if [ -d .git ]; then
    # Fetch full Git history if needed, e.g. when run via Travis-CI.
    git fetch --unshallow 2>/dev/null

    # Update doc/AUTHORS.adoc with Git commit authors plus authors mentioned via
    # a "Co-authored-by:" in the commit message.
    (git log | grep -Po "(?<=Co-authored-by: )(.*)(?= <)"; \
     git log --format="%aN") \
        | sed 's/^/* /' \
        | LANG=en_US.utf8 sort -uf \
        | perl -00 -p -i -e 's/^\*.*/<STDIN> . "\n"/es' doc/AUTHORS.adoc
fi
