#!/bin/sh -ex
set -e

if [ -d .git ]; then
  echo "Updating authors...\n"

  # Full git history is needed for this to work
  # (hide stderr and return code since this fails in case full history is already available)
  git fetch --unshallow 2> /dev/null || true

  # collect "Co-authored-by" and Authors and update doc/AUTHORS.adoc
  { git log --pretty=format:"%(trailers)" | grep -Po "(?<=Co-authored-by: )(.*)(?= <)"; git log --format="%aN"; } | sed -e 's/^/* /' | sort -udf | perl -00 -p -i -e 's/^\*.*/<STDIN> . "\n"/es' doc/AUTHORS.adoc
fi
