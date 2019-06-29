SUITE_no_compression_SETUP() {
    generate_code 1 test.c

    unset CCACHE_NODIRECT
    export CCACHE_NOCOMPRESS=1
}

SUITE_no_compression() {
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
    TEST "Result file is uncompressed"

    $CCACHE_COMPILE -c test.c
    result_file=$(find $CCACHE_DIR -name '*.result')
    if ! $CCACHE --dump-result $result_file | grep 'Compression type: none' >/dev/null 2>&1; then
        test_failed "Result file not uncompressed according to metadata"
    fi
    if [ $(file_size $result_file) -le $(file_size test.o) ]; then
        test_failed "Result file seems to be compressed"
    fi

    # -------------------------------------------------------------------------
    TEST "Hash sum equal for compressed and uncompressed files"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    unset CCACHE_NOCOMPRESS
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
}
