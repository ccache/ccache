SUITE_inode_cache_SETUP() {
    generate_code 1 test1.c
    export CCACHE_INODECACHE=1
    unset CCACHE_NODIRECT
}

SUITE_inode_cache() {
    inode_cache_tests
}

inode_cache_tests() {
    # -------------------------------------------------------------------------
    TEST "Initial stats"

    # Initial stats should be zero
    expect_stat 'inode cache hits' 0
    expect_stat 'inode cache misses' 0
    expect_stat 'inode cache errors' 0

    # -------------------------------------------------------------------------
    TEST "Cache miss"

    $CCACHE_COMPILE -c test1.c

    # First compilation should have no hits
    expect_stat 'inode cache hits' 0
    expect_stat 'inode cache errors' 0

    # -------------------------------------------------------------------------
    TEST "Cache hit"

    $CCACHE_COMPILE -c test1.c
    $CCACHE -z
    $CCACHE_COMPILE -c test1.c

    # Second compilation should have no misses
    expect_stat 'inode cache misses' 0
    expect_stat 'inode cache errors' 0

    # -------------------------------------------------------------------------
    TEST "Touched"

    $CCACHE_COMPILE -c test1.c
    $CCACHE -z
    backdate test1.c
    $CCACHE_COMPILE -c test1.c

    # Updated mtime should cause a miss
    expect_stat 'inode cache misses' 1
    expect_stat 'inode cache errors' 0

    # -------------------------------------------------------------------------
    TEST "Linked"

    ln test1.c test2.c
    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test2.c
    $CCACHE -z
    backdate test1.c
    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test2.c

    # Updated mtime should cause one miss, not two
    expect_stat 'inode cache misses' 1
    expect_stat 'inode cache errors' 0
}
