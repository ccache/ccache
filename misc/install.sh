#!/bin/sh

set -eu

script_dir="$(cd "$(dirname "$0")" && pwd)"

prefix="${prefix:-/usr/local}"
exec_prefix="${exec_prefix:-}"
bindir="${bindir:-}"
datarootdir="${datarootdir:-}"
docdir="${docdir:-}"
libexecdir="${libexecdir:-}"
mandir="${mandir:-}"
man1dir="${man1dir:-}"
sysconfdir="${sysconfdir:-}"
destdir="${DESTDIR:-}"
PYTHON="${PYTHON:-python3}"

doc_files="
    GPL-3.0.txt
    LICENSE.html
    LICENSE.md
    MANUAL.html
    MANUAL.md
    NEWS.html
    NEWS.md
    README.md
"

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Install ccache from a binary release.

Installation directories (following GNU conventions):
  --prefix=DIR         Top-level installation prefix [${prefix}]
  --exec-prefix=DIR    Exec prefix [PREFIX]
  --bindir=DIR         User executables [EXEC_PREFIX/bin]
  --datarootdir=DIR    Read-only architecture-independent data [PREFIX/share]
  --docdir=DIR         Documentation [DATAROOTDIR/doc/ccache]
  --libexecdir=DIR     Program executables [EXEC_PREFIX/libexec]
  --mandir=DIR         Man documentation [DATAROOTDIR/man]
  --sysconfdir=DIR     Read-only single-machine data [PREFIX/etc]

Other options:
  --destdir=DIR        Stage installation under DIR
  -h, --help           Print this help and exit

Environment variables:
  PYTHON               Python interpreter to use [python3]
EOF
}

error() {
    printf '%s: error: %s\n' "$0" "$1" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    arg=$1
    case ${arg} in
        --bindir=*)      bindir=${arg#*=} ;;
        --bindir)        shift; bindir=$1 ;;
        --datarootdir=*) datarootdir=${arg#*=} ;;
        --datarootdir)   shift; datarootdir=$1 ;;
        --destdir=*)     destdir=${arg#*=} ;;
        --destdir)       shift; destdir=$1 ;;
        --docdir=*)      docdir=${arg#*=} ;;
        --docdir)        shift; docdir=$1 ;;
        --exec-prefix=*) exec_prefix=${arg#*=} ;;
        --exec-prefix)   shift; exec_prefix=$1 ;;
        --libexecdir=*)  libexecdir=${arg#*=} ;;
        --libexecdir)    shift; libexecdir=$1 ;;
        --mandir=*)      mandir=${arg#*=} ;;
        --mandir)        shift; mandir=$1 ;;
        --prefix=*)      prefix=${arg#*=} ;;
        --prefix)        shift; prefix=$1 ;;
        --sysconfdir=*)  sysconfdir=${arg#*=} ;;
        --sysconfdir)    shift; sysconfdir=$1 ;;
        -h|--help)       usage; exit 0 ;;
        *)               error "unknown option: ${arg}" ;;
    esac
    shift
done

: "${exec_prefix:=${prefix}}"
: "${bindir:=${exec_prefix}/bin}"
: "${datarootdir:=${prefix}/share}"
: "${docdir:=${datarootdir}/doc/ccache}"
: "${libexecdir:=${exec_prefix}/libexec}"
: "${mandir:=${datarootdir}/man}"
: "${man1dir:=${mandir}/man1}"
: "${sysconfdir:=${prefix}/etc}"

command -v "${PYTHON}" >/dev/null 2>&1 || error "Python interpreter not found: ${PYTHON}"

echo "Installing binary: ${destdir}${bindir}/ccache"
mkdir -p "${destdir}${bindir}"
"${PYTHON}" "${script_dir}/patch-binary.py" "$libexecdir" "$sysconfdir" \
    <"${script_dir}/ccache" >"${destdir}${bindir}/ccache"
chmod 755 "${destdir}${bindir}/ccache"

echo "Installing documentation: ${destdir}${docdir}"
mkdir -p "${destdir}${docdir}"
for f in ${doc_files}; do
    cp "${script_dir}/${f}" "${destdir}${docdir}"
done

echo "Installing man page: ${destdir}${man1dir}/ccache.1"
mkdir -p "${destdir}${man1dir}"
cp "${script_dir}/ccache.1" "${destdir}${man1dir}"

echo "Installation complete."
