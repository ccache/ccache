SUITE_fileclone_PROBE() {
    touch file1
    if ! cp --reflink=always file1 file2 >/dev/null 2>&1; then
        echo "file system doesn't support file cloning"
    fi
}

SUITE_fileclone() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    generate_code 1 test.c

    $REAL_COMPILER -c -o reference_test.o test.c

    CCACHE_FILECLONE=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o

    # Note: CCACHE_DEBUG=1 below is needed for the test case.
    CCACHE_FILECLONE=1 CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o
    if ! grep -q 'Cloning.*to test.o' test.o.ccache-log; then
        test_failed "Did not try to clone file"
    fi
    if grep -q 'Failed to clone' test.o.ccache-log; then
        test_failed "Failed to clone"
    fi

    # -------------------------------------------------------------------------
    TEST "Cloning not used for stored non-raw result"

    generate_code 1 test.c

    $REAL_COMPILER -c -o reference_test.o test.c

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test.o test.o

    # Note: CCACHE_DEBUG=1 below is needed for the test case.
    CCACHE_FILECLONE=1 CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test.o test.o
    if grep -q 'Cloning' test.o.ccache-log; then
        test_failed "Tried to clone"
    fi
}
