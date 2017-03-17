#!/bin/sh
#
# A simple test suite for ccache.
#
# Copyright (C) 2002-2007 Andrew Tridgell
# Copyright (C) 2009-2017 Joel Rosdahl
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

green() {
    printf "\033[1;32m$*\033[0;0m\n"
}

red() {
    printf "\033[1;31m$*\033[0;0m\n"
}

bold() {
    printf "\033[1;37m$*\033[0;0m\n"
}

test_failed() {
    echo
    red FAILED
    echo
    echo "Test suite:     $(bold $CURRENT_SUITE)"
    echo "Test case:      $(bold $CURRENT_TEST)"
    echo "Failure reason: $(red $1)"
    echo
    echo "ccache -s:"
    $CCACHE -s
    echo
    echo "Test data and log file have been left in $TESTDIR"
    exit 1
}

generate_code() {
    local nlines=$1
    local outfile=$2

    rm -f $outfile
    for i in $(seq $nlines); do
        echo "int foo_$i(int x) { return x; }" >>$outfile
    done
}

remove_cache() {
    if [ -d $CCACHE_DIR ]; then
        chmod -R +w $CCACHE_DIR
        rm -rf $CCACHE_DIR
    fi
}

clear_cache() {
    $CCACHE -Cz >/dev/null
}

sed_in_place() {
    local expr=$1
    shift

    for file in $*; do
        sed "$expr" $file >$file.sed
        mv $file.sed $file
    done
}

backdate() {
    touch -t 199901010000 "$@"
}

expect_stat() {
    local stat="$1"
    local expected_value="$2"
    local value="$(echo $($CCACHE -s | fgrep "$stat" | cut -c34-))"

    if [ "$expected_value" != "$value" ]; then
        test_failed "Expected \"$stat\" to be $expected_value, actual $value"
    fi
}

expect_equal_files() {
    if [ ! -e "$1" ]; then
        test_failed "compare_files: $1 missing"
    fi
    if [ ! -e "$2" ]; then
        test_failed "compare_files: $2 missing"
    fi
    if ! cmp -s "$1" "$2"; then
        test_failed "compare_files:: $1 and $2 differ"
    fi
}

expect_equal_object_files() {
    if $HOST_OS_LINUX && $COMPILER_TYPE_CLANG; then
        if ! which eu-elfcmp >/dev/null 2>&1; then
            test_failed "Please install elfutils to get eu-elfcmp"
        fi
        eu-elfcmp -q "$1" "$2"
    else
        cmp -s "$1" "$2"
    fi
    if [ $? -ne 0 ]; then
        test_failed "Objects differ: $1 != $2"
    fi
}

expect_file_content() {
    local file="$1"
    local content="$2"

    if [ ! -f "$file" ]; then
        test_failed "$file not found"
    fi
    if [ "$(cat $file)" != "$content" ]; then
        test_failed "Bad content of $file.\nExpected: $content\nActual: $(cat $file)"
    fi
}

expect_file_count() {
    local expected=$1
    local pattern=$2
    local dir=$3
    local actual=`find $dir -name "$pattern" | wc -l`
    if [ $actual -ne $expected ]; then
        test_failed "Found $actual (expected $expected) $pattern files in $dir"
    fi
}

run_suite() {
    local name=$1

    CURRENT_SUITE=$name
    UNCACHED_COMPILE=uncached_compile

    cd $ABS_TESTDIR
    rm -rf $ABS_TESTDIR/fixture

    if type SUITE_${name}_PROBE >/dev/null 2>&1; then
        mkdir $ABS_TESTDIR/probe
        cd $ABS_TESTDIR/probe
        local skip_reason="$(SUITE_${name}_PROBE)"
        cd $ABS_TESTDIR
        rm -rf $ABS_TESTDIR/probe
        if [ -n "$skip_reason" ]; then
            echo "Skipped test suite $name [$skip_reason]"
            return
        fi
    fi

    printf "Running test suite %s" "$(bold $name)"
    SUITE_$name
    echo
}

uncached_compile() {
    # $COMPILER could be a masquerading system ccache, so make sure it's
    # disabled:
    CCACHE_DISABLE=1 $COMPILER "$@"
}

TEST() {
    CURRENT_TEST=$1

    unset CCACHE_BASEDIR
    unset CCACHE_CC
    unset CCACHE_COMMENTS
    unset CCACHE_COMPILERCHECK
    unset CCACHE_COMPRESS
    unset CCACHE_CPP2
    unset CCACHE_DIR
    unset CCACHE_DISABLE
    unset CCACHE_EXTENSION
    unset CCACHE_EXTRAFILES
    unset CCACHE_HARDLINK
    unset CCACHE_IGNOREHEADERS
    unset CCACHE_LIMIT_MULTIPLE
    unset CCACHE_LOGFILE
    unset CCACHE_NLEVELS
    unset CCACHE_NOCPP2
    unset CCACHE_NOHASHDIR
    unset CCACHE_NOSTATS
    unset CCACHE_PATH
    unset CCACHE_PREFIX
    unset CCACHE_PREFIX_CPP
    unset CCACHE_READONLY
    unset CCACHE_READONLY_DIRECT
    unset CCACHE_RECACHE
    unset CCACHE_SLOPPINESS
    unset CCACHE_TEMPDIR
    unset CCACHE_UMASK
    unset CCACHE_UNIFY
    unset GCC_COLORS

    export CCACHE_CONFIGPATH=$ABS_TESTDIR/ccache.conf
    export CCACHE_DETECT_SHEBANG=1
    export CCACHE_DIR=$ABS_TESTDIR/.ccache
    export CCACHE_LOGFILE=$ABS_TESTDIR/ccache.log
    export CCACHE_NODIRECT=1

    # Many tests backdate files, which updates their ctimes. In those tests, we
    # must ignore ctimes. Might as well do so everywhere.
    DEFAULT_SLOPPINESS=include_file_ctime
    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS"

    CCACHE_COMPILE="$CCACHE $COMPILER"

    if $VERBOSE; then
        printf "\n  %s" $CURRENT_TEST
    else
        printf .
    fi

    cd /
    remove_cache
    rm -rf $ABS_TESTDIR/run
    mkdir $ABS_TESTDIR/run
    cd $ABS_TESTDIR/run
    if type SUITE_${name}_SETUP >/dev/null 2>&1; then
        SUITE_${name}_SETUP
    fi
}

# =============================================================================

base_tests() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Debug option"

    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c -g
    expect_equal_object_files reference_test1.o reference_test1.o

    # -------------------------------------------------------------------------
    TEST "Output option"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c -o foo.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o foo.o

    # -------------------------------------------------------------------------
    TEST "Called for link"

    $CCACHE_COMPILE test1.c -o test 2>/dev/null
    expect_stat 'called for link' 1

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE test1.o -o test 2>/dev/null
    expect_stat 'called for link' 2

    # -------------------------------------------------------------------------
    TEST "No input file"

    $CCACHE_COMPILE -c foo.c 2>/dev/null
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "Called for preprocessing"

    $CCACHE_COMPILE -E -c test1.c >/dev/null 2>&1
    expect_stat 'called for preprocessing' 1

    # -------------------------------------------------------------------------
    TEST "Multiple source files"

    touch test2.c
    $CCACHE_COMPILE -c test1.c test2.c
    expect_stat 'multiple source files' 1

    # -------------------------------------------------------------------------
    TEST "Couldn't find the compiler"

    $CCACHE blahblah -c test1.c 2>/dev/null
    expect_stat "couldn't find the compiler" 1

    # -------------------------------------------------------------------------
    TEST "Bad compiler arguments"

    $CCACHE_COMPILE -c test1.c -I 2>/dev/null
    expect_stat 'bad compiler arguments' 1

    # -------------------------------------------------------------------------
    TEST "Unsupported source language"

    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2>/dev/null
    expect_stat 'unsupported source language' 1

    # -------------------------------------------------------------------------
    TEST "Unsupported compiler option"

    $CCACHE_COMPILE -M foo -c test1.c >/dev/null 2>&1
    expect_stat 'unsupported compiler option' 1

    # -------------------------------------------------------------------------
    TEST "Compiler produced stdout"

    $CCACHE echo foo -c test1.c >/dev/null
    expect_stat 'compiler produced stdout' 1

    # -------------------------------------------------------------------------
    TEST "Output to a non-regular file"

    mkdir testd
    $CCACHE_COMPILE -o testd -c test1.c >/dev/null 2>&1
    rmdir testd >/dev/null 2>&1
    expect_stat 'output to a non-regular file' 1

    # -------------------------------------------------------------------------
    TEST "No input file"

    $CCACHE_COMPILE -c -O2 2>/dev/null
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_DISABLE"

    CCACHE_DISABLE=1 $CCACHE_COMPILE -c test1.c 2>/dev/null
    if [ -d $CCACHE_DIR ]; then
        test_failed "$CCACHE_DIR created despite CCACHE_DISABLE being set"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMMENTS"

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c

    mv test1.c test1-saved.c
    echo '// initial comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    echo '// different comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    mv test1-saved.c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOSTATS"

    CCACHE_NOSTATS=1 $CCACHE_COMPILE -c test1.c -O -O
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # CCACHE_RECACHE replaces the object file, so the statistics counter will
    # be off-by-one until next cleanup.
    expect_stat 'files in cache' 2
    $CCACHE -c >/dev/null
    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "Directory is hashed if using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if not using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOHASHDIR"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_UNIFY"

    echo '// a silly comment' >>test1.c
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo '// another silly comment' >>test1.c
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_NLEVELS"

    CCACHE_NLEVELS=4 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    CCACHE_NLEVELS=4 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    # Directories in $CCACHE_DIR:
    # - .
    # - tmp
    # - a
    # - a/b
    # - a/b/c
    # - a/b/c/d
    actual_dirs=$(find $CCACHE_DIR -type d | wc -l)
    expected_dirs=6
    if [ $actual_dirs -ne $expected_dirs ]; then
        test_failed "Expected $expected_dirs directories, found $actual_dirs"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_EXTRAFILES"

    echo a >a
    echo b >b

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    echo b2 >b

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 3

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 3
    expect_stat 'cache miss' 3

    CCACHE_EXTRAFILES="doesntexist" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 3
    expect_stat 'cache miss' 3
    expect_stat 'error hashing extra file' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_PREFIX"

    cat <<'EOF' >prefix-a
#!/bin/sh
echo a >>prefix.result
exec "$@"
EOF
    cat <<'EOF' >prefix-b
#!/bin/sh
echo b >>prefix.result
exec "$@"
EOF
    chmod +x prefix-a prefix-b
    cat <<'EOF' >file.c
int foo;
EOF
    PATH=.:$PATH CCACHE_PREFIX="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    PATH=.:$PATH CCACHE_PREFIX="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    rm -f prefix.result
    PATH=.:$PATH CCACHE_PREFIX_CPP="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    # -------------------------------------------------------------------------
    TEST "Files in cache"

    for i in $(seq 32); do
        generate_code $i test$i.c
        $CCACHE_COMPILE -c test$i.c
    done
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 32
    expect_stat 'files in cache' 32

    # -------------------------------------------------------------------------
    TEST "Called for preprocessing"

    $CCACHE_COMPILE -c test1.c -E >test1.i
    expect_stat 'called for preprocessing' 1

    # -------------------------------------------------------------------------
    TEST "Direct .i compile"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $UNCACHED_COMPILE -c test1.c -E >test1.i
    $CCACHE_COMPILE -c test1.i
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x c"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-xc"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x none"

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x unknown"

    $CCACHE_COMPILE -x unknown -c test1.c 2>/dev/null
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported source language' 1

    # -------------------------------------------------------------------------
    TEST "-D not hashed"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -DNOT_AFFECTING=1 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-S"

    $CCACHE_COMPILE -S test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -S test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_PATH"

    override_path=`pwd`/override_path
    mkdir $override_path
    cat >$override_path/cc <<EOF
#!/bin/sh
touch override_path_compiler_executed
EOF
    chmod +x $override_path/cc
    CCACHE_PATH=$override_path $CCACHE cc -c test1.c
    if [ ! -f override_path_compiler_executed ]; then
        test_failed "CCACHE_PATH had no effect"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=mtime"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
# A comment
EOF
    chmod +x compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    sed_in_place 's/comment/yoghurt/' compiler.sh # Don't change the size
    chmod +x compiler.sh
    backdate compiler.sh # Don't change the timestamp

    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    touch compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=content"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    echo "# Compiler upgrade" >>compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=none"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=string"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=command"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK='echo %compiler%' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK="echo ./compiler.sh" $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cat <<EOF >foobar.sh
#!/bin/sh
echo foo
echo bar
EOF
    chmod +x foobar.sh
    CCACHE_COMPILERCHECK='./foobar.sh' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_COMPILERCHECK='echo foo; echo bar' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=unknown_command"

    cat >compiler.sh <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK="unknown_command" $CCACHE ./compiler.sh -c test1.c 2>/dev/null
    if [ "$?" -eq 0 ]; then
        test_failed "Expected failure running unknown_command to verify compiler but was success"
    fi
    expect_stat 'compiler check failed' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE should remove previous .stderr"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 0 ]; then
        test_failed "$num stderr files found, expected 0 (#1)"
    fi

    obj_file=`find $CCACHE_DIR -name '*.o'`
    stderr_file=`echo $obj_file | sed 's/..$/.stderr/'`
    echo "Warning: foo" >$stderr_file
    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c
    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 0 ]; then
        test_failed "$num stderr files found, expected 0 (#2)"
    fi

    # -------------------------------------------------------------------------
    TEST "No object file"

    cat <<'EOF' >test_no_obj.c
int test_no_obj;
EOF
    cat <<'EOF' >prefix-remove.sh
#!/bin/sh
"$@"
[ x$2 = x-fcolor-diagnostics ] && shift
[ x$2 = x-fdiagnostics-color ] && shift
[ x$2 = x-std=gnu99 ] && shift
[ x$3 = x-o ] && rm $4
EOF
    chmod +x prefix-remove.sh
    CCACHE_PREFIX=`pwd`/prefix-remove.sh $CCACHE_COMPILE -c test_no_obj.c
    expect_stat 'compiler produced no output' 1

    # -------------------------------------------------------------------------
    TEST "Empty object file"

    cat <<'EOF' >test_empty_obj.c
int test_empty_obj;
EOF
    cat <<'EOF' >prefix-empty.sh
#!/bin/sh
"$@"
[ x$2 = x-fcolor-diagnostics ] && shift
[ x$2 = x-fdiagnostics-color ] && shift
[ x$2 = x-std=gnu99 ] && shift
[ x$3 = x-o ] && cp /dev/null $4
EOF
    chmod +x prefix-empty.sh
    CCACHE_PREFIX=`pwd`/prefix-empty.sh $CCACHE_COMPILE -c test_empty_obj.c
    expect_stat 'compiler produced empty output' 1

    # -------------------------------------------------------------------------
    TEST "Caching stderr"

    cat <<EOF >stderr.c
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF
    $CCACHE_COMPILE -Wall -W -c stderr.c 2>/dev/null
    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 1 ]; then
        test_failed "$num stderr files found, expected 1"
    fi
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "--zero-stats"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE -z >/dev/null
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "--clear"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE -C >/dev/null
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0

    # -------------------------------------------------------------------------
    TEST "-P"

    # Check that -P disables ccache. (-P removes preprocessor information in
    # such a way that the object file from compiling the preprocessed file will
    # not be equal to the object file produced when compiling without ccache.)

    $CCACHE_COMPILE -c -P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 1

    # -------------------------------------------------------------------------
    TEST "-Wp,-P"

    # Check that -Wp,-P disables ccache. (-P removes preprocessor information
    # in such a way that the object file from compiling the preprocessed file
    # will not be equal to the object file produced when compiling without
    # ccache.)

    $CCACHE_COMPILE -c -Wp,-P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 1

    $CCACHE_COMPILE -c -Wp,-P,-DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 2

    $CCACHE_COMPILE -c -Wp,-DFOO,-P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 3

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Buggy GCC 6 cpp"

    cat >buggy-cpp <<EOF
#!/bin/sh
export CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
if echo "\$*" | grep -- -D >/dev/null; then
  $COMPILER "\$@"
else
  # Mistreat the preprocessor output in the same way as GCC 6 does.
  $COMPILER "\$@" |
    sed -e '/^# 1 "<command-line>"\$/ a\\
# 31 "<command-line>"' \\
        -e 's/^# 1 "<command-line>" 2\$/# 32 "<command-line>" 2/'
fi
exit 0
EOF
    cat <<'EOF' >file.c
int foo;
EOF
    chmod +x buggy-cpp

    $CCACHE ./buggy-cpp -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE ./buggy-cpp -DNOT_AFFECTING=1 -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Symlink to source directory"

    mkdir dir
    cd dir
    mkdir -p d1/d2
    echo '#define A "OK"' >d1/h.h
    cat <<EOF >d1/d2/c.c
#include <stdio.h>
#include "../h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "BUG"' >h.h
    ln -s d1/d2 d3

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/d3/c.c
    $UNCACHED_COMPILE c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi

    # -------------------------------------------------------------------------
    TEST "Symlink to source file"

    mkdir dir
    cd dir
    mkdir d
    echo '#define A "BUG"' >d/h.h
    cat <<EOF >d/c.c
#include <stdio.h>
#include "h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "OK"' >h.h
    ln -s d/c.c c.c

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/c.c
    $UNCACHED_COMPILE c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi

    # -------------------------------------------------------------------------
    TEST ".incbin"

    cat <<EOF >incbin.c
char x[] = ".incbin";
EOF

    $CCACHE_COMPILE -c incbin.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported code directive' 1
}

# =============================================================================

SUITE_base_SETUP() {
    generate_code 1 test1.c
}

SUITE_base() {
    base_tests
}

# =============================================================================

SUITE_nocpp2_SETUP() {
    export CCACHE_NOCPP2=1
    generate_code 1 test1.c
}

SUITE_nocpp2() {
    base_tests
}

# =============================================================================

SUITE_multi_arch_PROBE() {
    if ! $HOST_OS_APPLE; then
        echo "multiple -arch options not supported on $(uname -s)"
        return
    fi
}

SUITE_multi_arch_SETUP() {
    generate_code 1 test1.c
    unset CCACHE_NODIRECT
}

SUITE_multi_arch() {
    # -------------------------------------------------------------------------
    TEST "cache hit, direct mode"

    # Different arches shouldn't affect each other
    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "cache hit, preprocessor mode"

    export CCACHE_NODIRECT=1

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 3
}

# =============================================================================

SUITE_serialize_diagnostics_PROBE() {
    touch test.c
    if ! $UNCACHED_COMPILE -c --serialize-diagnostics \
         test1.dia test.c 2>/dev/null; then
        echo "--serialize-diagnostics not supported by compiler"
    fi
}

SUITE_serialize_diagnostics_SETUP() {
    generate_code 1 test1.c
}

SUITE_serialize_diagnostics() {
    # -------------------------------------------------------------------------
    TEST "Compile OK"

    $UNCACHED_COMPILE -c --serialize-diagnostics expected.dia test1.c

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_files expected.dia test.dia

    rm test.dia

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_files expected.dia test.dia

    # -------------------------------------------------------------------------
    TEST "Compile failed"

    echo "bad source" >error.c
    if $UNCACHED_COMPILE -c --serialize-diagnostics expected.dia error.c 2>expected.stderr; then
        test_failed "Expected an error compiling error.c"
    fi

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia error.c 2>test.stderr
    expect_stat 'compile failed' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 0
    expect_equal_files expected.dia test.dia
    expect_equal_files expected.stderr test.stderr

    # -------------------------------------------------------------------------
    TEST "--serialize-diagnostics + CCACHE_BASEDIR"

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2
    backdate dir1/include/test.h dir2/include/test.h

    cat <<EOF >stderr.h
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF

    unset CCACHE_NODIRECT

    cd dir1
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -w -MD -MF `pwd`/test.d -I`pwd`/include --serialize-diagnostics `pwd`/test.dia -c src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 4

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -w -MD -MF `pwd`/test.d -I`pwd`/include --serialize-diagnostics `pwd`/test.dia -c src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 4
}

# =============================================================================

SUITE_debug_prefix_map_PROBE() {
    if ! $COMPILER_TYPE_GCC || $COMPILER_USES_MINGW; then
        echo "-fdebug-prefix-map not supported by compiler"
    fi
}

SUITE_debug_prefix_map_SETUP() {
    unset CCACHE_NODIRECT

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2
    backdate dir1/include/test.h dir2/include/test.h
}

SUITE_debug_prefix_map() {
    # -------------------------------------------------------------------------
    TEST "Mapping of debug info CWD"

    cd dir1
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=dir -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if grep -E "[^=]`pwd`[^=]" test.o >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=dir -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if grep -E "[^=]`pwd`[^=]" test.o >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi

    # -------------------------------------------------------------------------
    TEST "Multiple -fdebug-prefix-map"

    cd dir1
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=foobar -fdebug-prefix-map=foo=bar -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if grep -E "[^=]`pwd`[^=]" test.o >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
    if ! grep "foobar" test.o >/dev/null 2>&1; then
        test_failed "Relocation (foobar) not found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=foobar -fdebug-prefix-map=foo=bar -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if grep -E "[^=]`pwd`[^=]" test.o >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
}

# =============================================================================

SUITE_masquerading_PROBE() {
    local compiler_binary=$(echo $COMPILER | cut -d' ' -f1)
    if [ "$(dirname $compiler_binary)" != . ]; then
        echo "compiler ($compiler_binary) not taken from PATH"
    fi
}

SUITE_masquerading_SETUP() {
    local compiler_binary=$(echo $COMPILER | cut -d' ' -f1)
    local compiler_args=$(echo $COMPILER | cut -s -d' ' -f2-)

    ln -s "$CCACHE" $compiler_binary
    CCACHE_COMPILE="./$compiler_binary $compiler_args"
    generate_code 1 test1.c
}

SUITE_masquerading() {
    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink"

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o
}

# =============================================================================

SUITE_hardlink_PROBE() {
    touch file1
    if ! ln file1 file2 >/dev/null 2>&1; then
        echo "file system doesn't support hardlinks"
    fi
}

SUITE_hardlink() {
    # -------------------------------------------------------------------------
    TEST "CCACHE_HARDLINK"

    generate_code 1 test1.c

    $UNCACHED_COMPILE -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    local obj_in_cache=$(find $CCACHE_DIR -name '*.o')
    if [ ! $obj_in_cache -ef test1.o ]; then
        test_failed "Object file not hard-linked to cached object file"
    fi
}

# =============================================================================

SUITE_direct_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >test.c
// test.c
#include "test1.h"
#include "test2.h"
EOF
    cat <<EOF >test1.h
#include "test3.h"
int test1;
EOF
    cat <<EOF >test2.h
int test2;
EOF
    cat <<EOF >test3.h
int test3;
EOF
    backdate test1.h test2.h test3.h

    $UNCACHED_COMPILE -c -Wp,-MD,expected.d test.c
    $UNCACHED_COMPILE -c -Wp,-MMD,expected_mmd.d test.c
    rm test.o
}

SUITE_direct() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $UNCACHED_COMPILE -c -o reference_test.o test.c

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # .o + .manifest
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "Corrupt manifest file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    manifest_file=`find $CCACHE_DIR -name '*.manifest'`
    rm $manifest_file
    touch $manifest_file

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_NODIRECT"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Modified include file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "int test3_2;" >>test3.h
    backdate test3.h
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Removed but previously compiled header file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    rm test3.h
    cat <<EOF >test1.h
// No more include of test3.h
int test1;
EOF
    backdate test1.h

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Calculation of dependency file names"

    mkdir test.dir
    for ext in .obj "" . .foo.bar; do
        dep_file=test.dir/`echo test$ext | sed 's/\.[^.]*\$//'`.d
        $CCACHE_COMPILE -MD -c test.c -o test.dir/test$ext
        rm -f $dep_file
        $CCACHE_COMPILE -MD -c test.c -o test.dir/test$ext
        if [ ! -f $dep_file ]; then
            test_failed "$dep_file missing"
        fi
    done
    expect_stat 'files in cache' 12

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    backdate a/source.h b/source.h
    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c
    expect_file_content source.d "source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    # -------------------------------------------------------------------------
    TEST "-MMD for different include file paths"

    mkdir a b
    touch a/source.h b/source.h
    backdate a/source.h b/source.h
    echo '#include <source.h>' >source.c
    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_file_content source.d "source.o: source.c a/source.h"

    $CCACHE_COMPILE -MMD -Ib -c source.c
    expect_file_content source.d "source.o: source.c b/source.h"

    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_file_content source.d "source.o: source.c a/source.h"

    # -------------------------------------------------------------------------
    TEST "-Wp,-MD"

    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d

    $UNCACHED_COMPILE -c -Wp,-MD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MD,different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-MMD"

    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_mmd.d

    $UNCACHED_COMPILE -c -Wp,-MMD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MMD,different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -DFOO test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-Wp, with multiple arguments"

    # ccache could try to parse and make sense of -Wp, with multiple arguments,
    # but it currently doesn't, so we have to disable direct mode.

    touch source.c

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content source.d "source.o: source.c"

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content source.d "source.o: source.c"

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c
    expect_file_content source.d "source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    # -------------------------------------------------------------------------
    TEST "Multiple object entries in manifest"

    for i in 0 1 2 3 4; do
        echo "int test1_$i;" >>test1.h
        backdate test1.h
        $CCACHE_COMPILE -c test.c
        $CCACHE_COMPILE -c test.c
    done
    expect_stat 'cache hit (direct)' 5
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 5

    # -------------------------------------------------------------------------
    TEST "-MD"

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d

    $UNCACHED_COMPILE -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d
    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage"

    cat <<EOF >code.c
int test() { return 0; }
EOF

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    test -r code.gcno || test_failed "code.gcno missing"

    rm code.gcno

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    test -r code.gcno || test_failed "code.gcno missing"

    # -------------------------------------------------------------------------
    TEST "Direct mode on cache created by ccache without direct mode support"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    $UNCACHED_COMPILE -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-MF"

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    $UNCACHED_COMPILE -c -MD -MF other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -MD -MF different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f different_name.d

    $CCACHE_COMPILE -c -MD -MFthird_name.d test.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files third_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f third_name.d

    # -------------------------------------------------------------------------
    TEST "Missing .d file"

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d

    find $CCACHE_DIR -name '*.d' -delete

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d

    # -------------------------------------------------------------------------
    TEST "stderr from both preprocessor and compiler"

    cat <<EOF >cpp-warning.c
#if FOO
// Trigger preprocessor warning about extra token after #endif.
#endif FOO
int stderr(void)
{
  // Trigger compiler warning by having no return statement.
}
EOF
    $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-orig.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-cpp.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content stderr-cpp.txt "`cat stderr-orig.txt`"

    $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content stderr-mf.txt "`cat stderr-orig.txt`"

    # -------------------------------------------------------------------------
    TEST "Empty source file"

    touch empty.c

    $CCACHE_COMPILE -c empty.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c empty.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Empty include file"

    touch empty.h
    cat <<EOF >include_empty.c
#include "empty.h"
EOF
    backdate empty.h
    $CCACHE_COMPILE -c include_empty.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c include_empty.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__FILE__ in source file disables direct mode"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF

    $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c `pwd`/file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "__FILE__ in include file disables direct mode"

    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF

    $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mv file_h.c file2_h.c

    $CCACHE_COMPILE -c `pwd`/file2_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "__FILE__ in source file ignored if sloppy"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c `pwd`/file.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__FILE__ in include file ignored if sloppy"

    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mv file_h.c file2_h.c

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c `pwd`/file2_h.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file disables direct mode"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in include file disables direct mode"

    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h

    cat <<EOF >time_h.c
#include "time.h"
EOF

    $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file ignored if sloppy"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in include file ignored if sloppy"

    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h
    cat <<EOF >time_h.c
#include "time.h"
EOF
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Too new include file disables direct mode"

    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h

    $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "New include file ignored if sloppy"

    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    # Check that environment variables that affect the preprocessor are taken
    # into account.
    TEST "CPATH included in hash"

    rm -rf subdir1 subdir2
    mkdir subdir1 subdir2
    cat <<EOF >subdir1/foo.h
int foo;
EOF
    cat <<EOF >subdir2/foo.h
int foo;
EOF
    cat <<EOF >foo.c
#include <foo.h>
EOF
    backdate subdir1/foo.h subdir2/foo.h

    CPATH=subdir1 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CPATH=subdir1 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2 # subdir2 is part of the preprocessor output

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Comment in strings"

    echo 'char *comment = " /* \\\\u" "foo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo 'char *comment = " /* \\\\u" "goo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "#line directives with troublesome files"

    cat <<EOF >strange.c
int foo;
EOF
    for x in stdout tty sda hda; do
        if [ -b /dev/$x ] || [ -c /dev/$x ]; then
            echo "#line 1 \"/dev/$x\"" >>strange.c
        fi
    done

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c strange.c

    manifest=`find $CCACHE_DIR -name '*.manifest'`
    if [ -n "$manifest" ]; then
        data="`$CCACHE --dump-manifest $manifest | egrep '/dev/(stdout|tty|sda|hda'`"
        if [ -n "$data" ]; then
            test_failed "$manifest contained troublesome file(s): $data"
        fi
    fi

    # -------------------------------------------------------------------------
    TEST "--dump-manifest"

    $CCACHE_COMPILE test.c -c -o test.o

    manifest=`find $CCACHE_DIR -name '*.manifest'`
    $CCACHE --dump-manifest $manifest >manifest.dump

    if grep 'Hash: d4de2f956b4a386c6660990a7a1ab13f' manifest.dump >/dev/null 2>&1 && \
       grep 'Hash: e94ceb9f1b196c387d098a5f1f4fe862' manifest.dump >/dev/null 2>&1 && \
       grep 'Hash: ba753bebf9b5eb99524bb7447095e2e6' manifest.dump >/dev/null 2>&1; then
        : OK
    else
        test_failed "Unexpected output of --dump-manifest"
    fi

    # -------------------------------------------------------------------------
    TEST "Argument-less -B and -L"

    cat <<EOF >test.c
#include <stdio.h>
int main(void)
{
#ifdef FOO
    puts("FOO");
#endif
    return 0;
}
EOF

    $CCACHE_COMPILE -B -L -DFOO -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -B -L -DBAR -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_IGNOREHEADERS"

    cat <<EOF >ignore.h
// We don't want this header in the manifest.
EOF
    backdate ignore.h
    cat <<EOF >ignore.c
#include "ignore.h"
int foo;
EOF

    CCACHE_IGNOREHEADERS="ignore.h" $CCACHE_COMPILE -c ignore.c
    manifest=`find $CCACHE_DIR -name '*.manifest'`
    data="`$CCACHE --dump-manifest $manifest | grep ignore.h`"
    if [ -n "$data" ]; then
        test_failed "$manifest contained ignored header: $data"
    fi
}

# =============================================================================

SUITE_basedir_SETUP() {
    unset CCACHE_NODIRECT

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2
    backdate dir1/include/test.h dir2/include/test.h
}

SUITE_basedir() {
    # -------------------------------------------------------------------------
    TEST "Enabled CCACHE_BASEDIR"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    cd ../dir2
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Disabled (default) CCACHE_BASEDIR"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # CCACHE_BASEDIR="" is the default:
    $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Path normalization"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # Rewriting triggered by CCACHE_BASEDIR should handle paths with multiple
    # slashes correctly:
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`//include -c `pwd`//src/test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Rewriting in stderr"

    cat <<EOF >stderr.h
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF
    backdate stderr.h
    cat <<EOF >stderr.c
#include <stderr.h>
EOF

    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi

    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi

    # -------------------------------------------------------------------------
    TEST "-MF/-MQ/-MT with absolute paths"

    for option in MF "MF " MQ "MQ " MT "MT "; do
        clear_cache
        cd dir1
        CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..

        cd dir2
        CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..
    done

    # -------------------------------------------------------------------------
    # When BASEDIR is set to /, check that -MF, -MQ and -MT arguments with
    # absolute paths are rewritten to relative and that the dependency file
    # only contains relative paths.
    TEST "-MF/-MQ/-MT with absolute paths and BASEDIR set to /"

    for option in MF "MF " MQ "MQ " MT "MT "; do
        clear_cache
        cd dir1
        CCACHE_BASEDIR="/" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        # Check that there is no absolute path in the dependency file:
        while read line; do
            for file in $line; do
                case $file in /*)
                    test_failed "Absolute file path '$file' found in dependency file '`pwd`/test.d'"
                esac
            done
        done <test.d
        cd ..

        cd dir2
        CCACHE_BASEDIR="/" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..
    done
}

# =============================================================================

SUITE_compression_SETUP() {
    generate_code 1 test.c
}

SUITE_compression() {
    # -------------------------------------------------------------------------
    TEST "Hash sum equal for compressed and uncompressed files"

    CCACHE_COMPRESS=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPRESS=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
}

# =============================================================================

SUITE_readonly_SETUP() {
    generate_code 1 test.c
    generate_code 2 test2.c
}

SUITE_readonly() {
    # -------------------------------------------------------------------------
    TEST "Cache hit"

    # Cache a compilation.
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm test.o

    # Make the cache read-only.
    chmod -R a-w $CCACHE_DIR

    # Check that read-only mode finds the cached result.
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp CCACHE_PREFIX=false $CCACHE_COMPILE -c test.c
    status1=$?

    # Check that fallback to the real compiler works for a cache miss.
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp $CCACHE_COMPILE -c test2.c
    status2=$?

    # Leave test dir a nice state after test failure.
    chmod -R +w $CCACHE_DIR

    if [ $status1 -ne 0 ]; then
        test_failed "Failure when compiling test.c read-only"
    fi
    if [ $status2 -ne 0 ]; then
        test_failed "Failure when compiling test2.c read-only"
    fi
    if [ ! -f test.o ]; then
        test_failed "test.o missing"
    fi
    if [ ! -f test2.o ]; then
        test_failed "test2.o missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Cache miss"

    # Check that read-only mode doesn't try to store new results.
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp $CCACHE_COMPILE -c test.c
    if [ $? -ne 0 ]; then
        test_failed "Failure when compiling test2.c read-only"
    fi
    if [ -d $CCACHE_DIR ]; then
        test_failed "ccache dir was created"
    fi

    # -------------------------------------------------------------------------
    # Check that read-only mode and direct mode work together.
    TEST "Cache hit, direct"

    # Cache a compilation.
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm test.o

    # Make the cache read-only.
    chmod -R a-w $CCACHE_DIR

    # Direct mode should work:
    files_before=`find $CCACHE_DIR -type f | wc -l`
    CCACHE_DIRECT=1 CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp $CCACHE_COMPILE -c test.c
    files_after=`find $CCACHE_DIR -type f | wc -l`

    # Leave test dir a nice state after test failure.
    chmod -R +w $CCACHE_DIR

    if [ $? -ne 0 ]; then
        test_failed "Failure when compiling test.c read-only"
    fi
    if [ $files_after -ne $files_before ]; then
        test_failed "Read-only mode + direct mode stored files in the cache"
    fi
}

# =============================================================================

SUITE_readonly_direct_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

SUITE_readonly_direct() {
    # -------------------------------------------------------------------------
    TEST "Direct hit"

    $CCACHE_COMPILE -c test.c -o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_READONLY_DIRECT=1 $CCACHE_COMPILE -c test.c -o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Direct miss doesn't lead to preprocessed hit"

    $CCACHE_COMPILE -c test.c -o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_READONLY_DIRECT=1 $CCACHE_COMPILE -DFOO -c test.c -o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
}

# =============================================================================

prepare_cleanup_test_dir() {
    local dir=$1

    rm -rf $dir
    mkdir -p $dir
    for i in $(seq 0 9); do
        printf '%4017s' '' | tr ' ' 'A' >$dir/result$i-4017.o
        touch $dir/result$i-4017.stderr
        touch $dir/result$i-4017.d
        if [ $i -gt 5 ]; then
            backdate $dir/result$i-4017.stderr
        fi
    done
    # NUMFILES: 30, TOTALSIZE: 40 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 30 40 0 0" >$dir/stats
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -C >/dev/null
    expect_file_count 0 '*.o' $CCACHE_DIR
    expect_file_count 0 '*.d' $CCACHE_DIR
    expect_file_count 0 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 0
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*.o' $CCACHE_DIR
    expect_file_count 10 '*.d' $CCACHE_DIR
    expect_file_count 10 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 30
    expect_stat 'cleanups performed' 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    # floor(0.8 * 9) = 7
    expect_file_count 7 '*.o' $CCACHE_DIR
    expect_file_count 7 '*.d' $CCACHE_DIR
    expect_file_count 7 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 21
    expect_stat 'cleanups performed' 1
    for i in 0 1 2 3 4 5 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 6 7 8; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, size limit"

    # NOTE: This test is known to fail on filesystems that have unusual block
    # sizes, including ecryptfs. The workaround is to place the test directory
    # elsewhere:
    #
    #     cd /tmp
    #     CCACHE=$DIR/ccache $DIR/test.sh

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # (4/10) * 10 * 4 * 16 = 256
    $CCACHE -F 0 -M 256K >/dev/null
    $CCACHE -c >/dev/null
    # floor(0.8 * 4) = 3
    expect_file_count 3 '*.o' $CCACHE_DIR
    expect_file_count 3 '*.d' $CCACHE_DIR
    expect_file_count 3 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 9
    expect_stat 'cleanups performed' 1
    for i in 3 4 5; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 0 1 2 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    expect_file_count 160 '*.o' $CCACHE_DIR
    expect_file_count 160 '*.d' $CCACHE_DIR
    expect_file_count 160 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 480

    touch empty.c
    $CCACHE_COMPILE -c empty.c -o empty.o
    # floor(0.8 * 9) = 7
    expect_file_count 157 '*.o' $CCACHE_DIR
    expect_file_count 156 '*.d' $CCACHE_DIR
    expect_file_count 156 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 469
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Cleanup of sibling files"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    backdate $CCACHE_DIR/a/result2-4017.stderr
    $CCACHE -c >/dev/null
    # floor(0.8 * 9) = 7
    expect_file_count 7 '*.o' $CCACHE_DIR
    expect_file_count 7 '*.d' $CCACHE_DIR
    expect_file_count 7 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 21
    for i in 0 1 3 4 5 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 2 6 7; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "No cleanup of new unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat 'files in cache' 31
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    if [ ! -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown removed"
    fi
    expect_stat 'files in cache' 19

    # -------------------------------------------------------------------------
    TEST "Cleanup of old unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    touch $CCACHE_DIR/a/abcd.unknown
    backdate $CCACHE_DIR/a/abcd.unknown
    $CCACHE -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown not removed"
    fi

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a
    touch $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat 'files in cache' 1
    backdate $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.tmp.efgh ]; then
        test_failed "$CCACHE_DIR/a/abcd.tmp.unknown not removed"
    fi
    expect_stat 'files in cache' 0

    # -------------------------------------------------------------------------
    TEST "No cleanup of .nfs* files"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/.nfs0123456789
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 1 '.nfs*' $CCACHE_DIR
    expect_stat 'files in cache' 30

    # -------------------------------------------------------------------------
    TEST "CCACHE_LIMIT_MULTIPLE"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # (1/1) * 30 * 16 = 480
    $CCACHE -F 480 >/dev/null
    CCACHE_LIMIT_MULTIPLE=0.5 $CCACHE -c >/dev/null
    expect_stat 'files in cache' 15
}

# =============================================================================

SUITE_pch_PROBE() {
    touch pch.h
    if ! $UNCACHED_COMPILE $SYSROOT -fpch-preprocess pch.h 2>/dev/null \
            || [ ! -f pch.h.gch ]; then
        echo "compiler ($($COMPILER --version | head -1)) doesn't support precompiled headers"
    fi
}

SUITE_pch_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >pch.c
#include "pch.h"
int main()
{
  void *p = NULL;
  return 0;
}
EOF
    cat <<EOF >pch.h
#include <stdlib.h>
EOF
    backdate pch.h
    cat <<EOF >pch2.c
int main()
{
  void *p = NULL;
  return 0;
}
EOF
}

SUITE_pch() {
    # Clang and GCC handle precompiled headers similarly, but GCC is much more
    # forgiving with precompiled headers. Both GCC and Clang keep an absolute
    # path reference to the original file except that Clang uses that reference
    # to validate the pch and GCC ignores the reference. Also, Clang has an
    # additional feature: pre-tokenized headers. For these reasons, Clang
    # should be tested differently from GCC. Clang can only use pch or pth
    # headers on the command line and not as an #include statement inside a
    # source file.

    if $COMPILER_TYPE_CLANG; then
        pch_suite_clang
    else
        pch_suite_gcc
    fi
}

pch_suite_gcc() {
    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, without opt-in"

    $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, without opt-in"

    $CCACHE_COMPILE pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, with opt-in"

    CCACHE_SLOPPINESS=pch_defines $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm pch.h.gch

    CCACHE_SLOPPINESS=pch_defines $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ ! -f pch.h.gch ]; then
        test_failed "pch.h.gch missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, with opt-in"

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ ! -f pch.gch ]; then
        test_failed "pch.gch missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, #include"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Preprocessor error because GCC can't find the real include file when
    # trying to preprocess:
    expect_stat 'preprocessor error' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, no sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, no sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
}

pch_suite_clang() {
    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, without opt-in"

    $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, without opt-in"

    $CCACHE_COMPILE pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, with opt-in"

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm pch.h.gch

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ ! -f pch.h.gch ]; then
        test_failed "pch.h.gch missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, with opt-in"

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ ! -f pch.gch ]; then
        test_failed "pch.gch missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, no sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, -include, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # clang seems to cope with this...
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # clang seems to cope with this...
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Create .pth, -c, -o"

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm -f pch.h.pth

    CCACHE_SLOPPINESS=pch_defines,time_macros $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ ! -f pch.h.pth ]; then
        test_failed "pch.h.pth missing"
    fi

    # -------------------------------------------------------------------------
    TEST "Use .pth, no -fpch-preprocess, -include, no sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    backdate pch.h.pth

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .pth, no -fpch-preprocess, -include, sloppiness"

    $UNCACHED_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    backdate pch.h.pth

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .pth, -fpch-preprocess, -include, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    backdate pch.h.pth

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.pth # clang seems to cope with this...
    backdate pch.h.pth

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .pth, preprocessor mode"

    $UNCACHED_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    backdate pch.h.pth

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .pth, preprocessor mode, file changed"

    $UNCACHED_COMPILE $SYSROOT -c pch.h -o pch.h.pth
    backdate pch.h.pth

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.pth # clang seems to cope with this...
    backdate pch.h.pth

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
}

# =============================================================================

SUITE_upgrade() {
    TEST "Keep maxfiles and maxsize settings"

    rm $CCACHE_CONFIGPATH
    mkdir -p $CCACHE_DIR/0
    echo "0 0 0 0 0 0 0 0 0 0 0 0 0 2000 131072" >$CCACHE_DIR/0/stats
    expect_stat 'max files' 32000
    expect_stat 'max cache size' '2.1 GB'
}

# =============================================================================

SUITE_input_charset_PROBE() {
    touch test.c
    if ! $UNCACHED_COMPILE -c -finput-charset=latin1 test.c >/dev/null 2>&1; then
        echo "compiler doesn't support -finput-charset"
    fi
}

SUITE_input_charset() {
    # -------------------------------------------------------------------------
    TEST "-finput-charset"

    printf '#include <wchar.h>\nwchar_t foo[] = L"\xbf";\n' >latin1.c

    $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    CCACHE_NOCPP2=1 $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_NOCPP2=1 $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2
}

# =============================================================================
# main program

if pwd | grep '[^A-Za-z0-9/.,=_%+-]' >/dev/null 2>&1; then
    cat <<EOF
Error: The test suite doesn't work in directories with whitespace or other
funny characters in the name. Sorry.
EOF
    exit 1
fi

if [ -n "$CC" ]; then
    COMPILER="$CC"
else
    COMPILER=gcc
fi
if [ -z "$CCACHE" ]; then
    CCACHE=`pwd`/ccache
fi

COMPILER_TYPE_CLANG=false
COMPILER_TYPE_GCC=false

COMPILER_USES_LLVM=false
COMPILER_USES_MINGW=false

HOST_OS_APPLE=false
HOST_OS_LINUX=false
HOST_OS_WINDOWS=false

compiler_version="`$COMPILER --version 2>&1 | head -1`"
case $compiler_version in
    *gcc*|*g++*|2.95*)
        COMPILER_TYPE_GCC=true
        ;;
    *clang*)
        COMPILER_TYPE_CLANG=true
        ;;
    *)
        echo "WARNING: Compiler $COMPILER not supported (version: $compiler_version) -- not running tests" >&2
        exit 0
        ;;
esac

case $compiler_version in
    *llvm*|*LLVM*)
        COMPILER_USES_LLVM=true
        ;;
    *MINGW*|*mingw*)
        COMPILER_USES_MINGW=true
        ;;
esac

case $(uname -s) in
    *MINGW*|*mingw*)
        HOST_OS_WINDOWS=true
        ;;
    *Darwin*)
        HOST_OS_APPLE=true
        ;;
    *Linux*)
        HOST_OS_LINUX=true
        ;;
esac

if $HOST_OS_WINDOWS; then
    PATH_DELIM=";"
else
    PATH_DELIM=":"
fi

if $HOST_OS_APPLE; then
    # Grab the developer directory from the environment or try xcode-select
    if [ "$XCODE_DEVELOPER_DIR" = "" ]; then
      XCODE_DEVELOPER_DIR=`xcode-select --print-path`
      if [ "$XCODE_DEVELOPER_DIR" = "" ]; then
        echo "Error: XCODE_DEVELOPER_DIR environment variable not set and xcode-select path not set"
        exit 1
      fi
    fi

    # Choose the latest SDK if an SDK root is not set
    MAC_PLATFORM_DIR=$XCODE_DEVELOPER_DIR/Platforms/MacOSX.platform
    if [ "$SDKROOT" = "" ]; then
        SDKROOT="`eval ls -f -1 -d \"$MAC_PLATFORM_DIR/Developer/SDKs/\"*.sdk | tail -1`"
        if [ "$SDKROOT" = "" ]; then
            echo "Error: Cannot find a valid SDK root directory"
            exit 1
        fi
    fi

    SYSROOT="-isysroot `echo \"$SDKROOT\" | sed 's/ /\\ /g'`"
else
    SYSROOT=
fi

# ---------------------------------------

TESTDIR=testdir.$$
ABS_TESTDIR=$PWD/$TESTDIR
rm -rf $TESTDIR
mkdir $TESTDIR
cd $TESTDIR || exit 1

# ---------------------------------------

all_suites="
base
nocpp2
multi_arch
serialize_diagnostics
debug_prefix_map
masquerading
hardlink
direct
basedir
compression
readonly
readonly_direct
cleanup
pch
upgrade
input_charset
"

compiler_location=$(which $(echo "$COMPILER" | awk '{print $1}'))
if [ "$compiler_location" = "$COMPILER" ]; then
    echo "Compiler:         $COMPILER"
else
    echo "Compiler:         $COMPILER ($compiler_location)"
fi
echo "Compiler version: $($COMPILER --version | head -n 1)"
echo

VERBOSE=false
[ "$1" = "-v" ] && { VERBOSE=true; shift; }

suites="$*"
if [ -z "$suites" ]; then
    suites="$all_suites"
fi

for suite in $suites; do
    run_suite $suite
done

cd /
rm -rf $ABS_TESTDIR
green PASSED
exit 0
