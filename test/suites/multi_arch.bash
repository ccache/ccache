SUITE_multi_arch_PROBE() {
    if ! $HOST_OS_APPLE; then
        echo "multiple -arch options not supported on $(uname -s)"
        return
    fi
}

SUITE_multi_arch_SETUP() {
    generate_code 1 test1.c
    unset CCACHE_NODIRECT
}

SUITE_multi_arch() {
    # -------------------------------------------------------------------------
    TEST "cache hit, direct mode"

    # Different arches shouldn't affect each other
    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 3

    # A single -Xarch_* matching -arch is supported.
    CCACHE_DEBUG=1 $CCACHE_COMPILE -arch x86_64 -Xarch_x86_64 -I. -c test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 4
    expect_contains     test1.o.*.ccache-log "clang -Xarch_x86_64 -I. -arch x86_64 -E"
    expect_not_contains test1.o.*.ccache-log "clang -Xarch_x86_64 -I. -I. -arch x86_64 -E"

    $CCACHE_COMPILE -arch x86_64 -Xarch_x86_64 -I. -c test1.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 4

    # -------------------------------------------------------------------------
    TEST "cache hit, preprocessor mode"

    export CCACHE_NODIRECT=1

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 3
}
