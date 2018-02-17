SUITE_memcached_only_PROBE() {
    probe_memcached
}

SUITE_memcached_only_SETUP() {
    export CCACHE_MEMCACHED_CONF=--SERVER=localhost:22122
    export CCACHE_MEMCACHED_ONLY=1

    generate_code 1 test1.c
    start_memcached -p 22122
}

SUITE_memcached_only() {
    # -------------------------------------------------------------------------
    TEST "Preprocessor hit"

    $REAL_COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0 # no object file stored in filesystem cache
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0 # no object file stored in filesystem cache
    expect_equal_object_files reference_test1.o test1.o

    # Disable memcache and check that we don't get a hit from filesystem cache:
    CCACHE_MEMCACHED_CONF="" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "Direct hit"

    unset CCACHE_NODIRECT

    $REAL_COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1 # manifest file
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1 # manifest file
    expect_equal_object_files reference_test1.o test1.o
}
