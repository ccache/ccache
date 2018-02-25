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
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "cache hit, preprocessor mode"

    export CCACHE_NODIRECT=1

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -arch i386 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # Multiple arches should be cached too
    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE -arch i386 -arch x86_64 -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 3
}
