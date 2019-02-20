SUITE_split_dwarf_PROBE() {
    touch test.c
    if ! $REAL_COMPILER -c -gsplit-dwarf test.c 2>/dev/null || [ ! -e test.dwo ]; then
        echo "-gsplit-dwarf not supported by compiler"
    fi
}


SUITE_split_dwarf_SETUP() {
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

SUITE_split_dwarf() {
    # -------------------------------------------------------------------------
    TEST "Directory is hashed if using -gsplit-dwarf"

    cd dir1
    $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -gsplit-dwarf
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -gsplit-dwarf
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -gsplit-dwarf
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -gsplit-dwarf
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "-fdebug-prefix-map and -gsplit-dwarf"

    cd dir1
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -gsplit-dwarf -fdebug-prefix-map=$(pwd)=. -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3
    if objdump_cmd test.o | grep_cmd "$(pwd)" >/dev/null 2>&1; then
        test_failed "Source dir ($(pwd)) found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -gsplit-dwarf -fdebug-prefix-map=$(pwd)=. -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3
    if objdump_cmd test.o | grep_cmd "$(pwd)" >/dev/null 2>&1; then
        test_failed "Source dir ($(pwd)) found in test.o"
    fi
}
