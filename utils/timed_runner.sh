#!/usr/bin/env bash
set -euo pipefail

# ===============================
# Default parameters
# ===============================
CCACHE="${CCACHE:-/ccache/build/ccache}"
CSV="${CSV:-compile_times.csv}"
CC="${CC:-gcc}"
SRC="${SRC:-main.c}"           # source file
OBJ="${OBJ:-main.o}"           # object file
EXE="${EXE:-main}"             # executable file
CFLAGS="${CFLAGS:--O2 -c}"     # compile flags
LINK_FLAGS="${LINK_FLAGS:-}"   # linker flags, e.g., -lm

# ===============================
# Usage info
# ===============================
usage() {
  echo "Usage: $0 [-c compiler] [-s src_file] [-o obj_file] [-e exe_file] [-f cflags] [-l link_flags] [-x csv_file] [-h]"
  exit 1
}

# ===============================
# Parse command line options
# ===============================
while getopts "c:s:o:e:f:l:x:h" opt; do
  case $opt in
    c) CC="$OPTARG" ;;
    s) SRC="$OPTARG" ;;
    o) OBJ="$OPTARG" ;;
    e) EXE="$OPTARG" ;;
    f) CFLAGS="$OPTARG" ;;
    l) LINK_FLAGS="$OPTARG" ;;
    x) CSV="$OPTARG" ;;
    h) usage ;;
    *) usage ;;
  esac
done

# ===============================
# Ensure CSV header exists
# ===============================
if [[ ! -f "$CSV" ]]; then
  echo "timestamp,elapsed_sec,user_sec,sys_sec,exit_status" >> "$CSV"
fi

# ===============================
# Compile and time
# ===============================
/usr/bin/time -f "$(date +%s),%e,%U,%S,%x" \
  $CCACHE $CC $CFLAGS "$SRC" -o "$OBJ" $LINK_FLAGS \
  2>> "$CSV"

# ===============================
# Show ccache stats
# ===============================
$CCACHE --show-stats --verbose
