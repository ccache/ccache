SUITE_reflink_PROBE() {
    if ! $HOST_OS_LINUX; then
      echo "reflink option not yet supported on $(uname -s)"
      return
    fi
    touch file1
    if ! cp --reflink=always file1 file2 >/dev/null 2>&1; then
        echo "file system doesn't support reflinks"
    fi
}

SUITE_reflink() {
    # -------------------------------------------------------------------------
    TEST "CCACHE_REFLINK"

    generate_code 1 test1.c

    $REAL_COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    CCACHE_REFLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o
}
