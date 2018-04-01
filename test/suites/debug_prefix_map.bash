SUITE_debug_prefix_map_PROBE() {
    touch test.c
    if ! $REAL_COMPILER -c -fdebug-prefix-map=old=new test.c 2>/dev/null; then
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

objdump_cmd() {
    if $HOST_OS_APPLE; then
        xcrun dwarfdump -r0 $1
    elif $HOST_OS_WINDOWS || $HOST_OS_CYGWIN; then
        strings $1 # for some reason objdump only shows the basename of the file, so fall back to brute force and ignorance
    else
        objdump -g $1
    fi
}

grep_cmd() {
    if $HOST_OS_APPLE; then
        grep "( \"$1\" )"
    elif $HOST_OS_WINDOWS || $HOST_OS_CYGWIN; then
        test -n "$2" && grep -E "$1|$2" || grep "$1" # accept a relative path for source code, in addition to relocation dir
    else
        grep ": $1[[:space:]]*$"
    fi
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
    if objdump_cmd test.o | grep_cmd "`pwd`" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
    if ! objdump_cmd test.o | grep_cmd "dir" src/test.c >/dev/null 2>&1; then
        test_failed "Relocation (dir) not found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=dir -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump_cmd test.o | grep_cmd "`pwd`" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi

    # -------------------------------------------------------------------------
    TEST "Multiple -fdebug-prefix-map"

    cd dir1
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=name -fdebug-prefix-map=foo=bar -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump_cmd test.o | grep_cmd "`pwd`" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
    if ! objdump_cmd test.o | grep_cmd "name" src/test.c >/dev/null 2>&1; then
        test_failed "Relocation (name) not found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=name -fdebug-prefix-map=foo=bar -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump_cmd test.o | grep_cmd "`pwd`" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
}
