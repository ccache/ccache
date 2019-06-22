SUITE_compression_SETUP() {
    generate_code 1 test.c

    unset CCACHE_NODIRECT
    export CCACHE_COMPRESS=1
}

SUITE_compression() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $REAL_COMPILER -c -o reference_test.o test.c

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "Hash sum equal for compressed and uncompressed files"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    unset CCACHE_COMPRESS
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
}
