SUITE_readonly_direct_PROBE() {
    mkdir dir
    chmod a-w dir
    if [ -w dir ]; then
        echo "File system doesn't support read-only mode"
    fi
    rmdir dir
}

SUITE_readonly_direct_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

SUITE_readonly_direct() {
    # -------------------------------------------------------------------------
    TEST "Direct hit"

    $CCACHE_COMPILE -c test.c -o test.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1

    CCACHE_READONLY_DIRECT=1 $CCACHE_COMPILE -c test.c -o test.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Direct miss doesn't lead to preprocessed hit"

    $CCACHE_COMPILE -c test.c -o test.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1

    CCACHE_READONLY_DIRECT=1 $CCACHE_COMPILE -DFOO -c test.c -o test.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat direct_cache_miss 2
    expect_stat preprocessed_cache_miss 1
}
