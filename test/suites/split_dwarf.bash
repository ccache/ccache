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
    TEST "Output filename is hashed if using -gsplit-dwarf"

    cd dir1

    $REAL_COMPILER -I$(pwd)/include -c src/test.c -o test.o -gsplit-dwarf
    mv test.o reference.o
    mv test.dwo reference.dwo

    $REAL_COMPILER -I$(pwd)/include -c src/test.c -o test.o -gsplit-dwarf
    mv test.o reference2.o
    mv test.dwo reference2.dwo

    if is_equal_object_files reference.o reference2.o; then
        $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -o test.o -gsplit-dwarf
        expect_equal_object_files reference.o test.o
        expect_equal_object_files reference.dwo test.dwo
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        expect_stat 'files in cache' 2

        $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -o test.o -gsplit-dwarf
        expect_equal_object_files reference.o test.o
        expect_equal_object_files reference.dwo test.dwo
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        expect_stat 'files in cache' 2

        $REAL_COMPILER -I$(pwd)/include -c src/test.c -o test2.o -gsplit-dwarf
        mv test2.o reference2.o
        mv test2.dwo reference2.dwo

        $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -o test2.o -gsplit-dwarf
        expect_equal_object_files reference2.o test2.o
        expect_equal_object_files reference2.dwo test2.dwo
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 2
        expect_stat 'files in cache' 4

        $CCACHE_COMPILE -I$(pwd)/include -c src/test.c -o test2.o -gsplit-dwarf
        expect_equal_object_files reference2.o test2.o
        expect_equal_object_files reference2.dwo test2.dwo
        expect_stat 'cache hit (direct)' 2
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 2
        expect_stat 'files in cache' 4
    fi
    # Else: Compiler does not produce stable object file output when compiling
    # the same source to the same output filename twice (DW_AT_GNU_dwo_id
    # differs), so we can't verify filename hashing.

    # -------------------------------------------------------------------------
    TEST "-fdebug-prefix-map and -gsplit-dwarf"

    cd dir1
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -gsplit-dwarf -fdebug-prefix-map=$(pwd)=. -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump_cmd test.o | grep_cmd "$(pwd)" >/dev/null 2>&1; then
        test_failed "Source dir ($(pwd)) found in test.o"
    fi

    cd ../dir2
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -gsplit-dwarf -fdebug-prefix-map=$(pwd)=. -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    if objdump_cmd test.o | grep_cmd "$(pwd)" >/dev/null 2>&1; then
        test_failed "Source dir ($(pwd)) found in test.o"
    fi
}
