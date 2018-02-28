SUITE_debug_prefix_map_PROBE() {
    if $COMPILER_USES_MINGW; then
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
    if objdump -g test.o | grep ": `pwd`$" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=dir -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump -g test.o | grep ": `pwd`$" >/dev/null 2>&1; then
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
    if objdump -g test.o | grep ": `pwd`$" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
    if ! objdump -g test.o | grep ": name$" >/dev/null 2>&1; then
        test_failed "Relocation (name) not found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`/include -g -fdebug-prefix-map=`pwd`=name -fdebug-prefix-map=foo=bar -c `pwd`/src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump -g test.o | grep ": `pwd`$" >/dev/null 2>&1; then
        test_failed "Source dir (`pwd`) found in test.o"
    fi
}
