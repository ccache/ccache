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

    $REAL_COMPILER -c -o reference_test1.o test1.c

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

    local obj_in_cache
    obj_in_cache=$(find $CCACHE_DIR -name '*.o')
    if [ ! $obj_in_cache -ef test1.o ]; then
        test_failed "Object file not hard-linked to cached object file"
    fi

    # -------------------------------------------------------------------------
    TEST "Overwrite assembler"

    generate_code 1 test1.c
    $REAL_COMPILER -S -o test1.s test1.c

    $REAL_COMPILER -c -o reference_test1.o test1.s

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    generate_code 2 test1.c
    $REAL_COMPILER -S -o test1.s test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2

    generate_code 1 test1.c
    $REAL_COMPILER -S -o test1.s test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Automake depend move"

    unset CCACHE_NODIRECT

    generate_code 1 test1.c

    CCACHE_HARDLINK=1 CCACHE_DEPEND=1 $CCACHE_COMPILE -c -MMD -MF test1.d.tmp test1.c
    expect_stat 'cache hit (direct)' 0
    mv test1.d.tmp test1.d || test_failed "first mv failed"

    CCACHE_HARDLINK=1 CCACHE_DEPEND=1 $CCACHE_COMPILE -c -MMD -MF test1.d.tmp test1.c
    expect_stat 'cache hit (direct)' 1
    mv test1.d.tmp test1.d || test_failed "second mv failed"

}
