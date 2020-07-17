SUITE_inode_cache_SETUP() {
    export CCACHE_INODECACHE=1
    export CCACHE_DEBUG=1
    unset CCACHE_NODIRECT
}

SUITE_inode_cache() {
    inode_cache_tests
}

expect_inode_cache_type() {
    local expected=$1
    local source_file=$2
    local type=$3

    local log_file=$(echo $source_file | sed 's/\.c$/.o.ccache-log/')
    local actual=$(grep -c "inode cache $type: $source_file" "$log_file")
    if [ $actual -ne $expected ]; then
        test_failed "Found $actual (expected $expected) $type for $source_file"
    fi
}

expect_inode_cache() {
    expect_inode_cache_type $1 $4 hit
    expect_inode_cache_type $2 $4 miss
    expect_inode_cache_type $3 $4 insert
}

inode_cache_tests() {
    # -------------------------------------------------------------------------
    TEST "Compile once"

    echo "// compile once" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c

    # -------------------------------------------------------------------------
    TEST "Recompile"

    echo "// recompile" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 1 0 0 test1.c

    # -------------------------------------------------------------------------
    TEST "Backdate"

    echo "// backdate" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c

    backdate test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c

    # -------------------------------------------------------------------------
    TEST "Hard link"

    echo "// hard linked" > test1.c
    ln -f test1.c test2.c
    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test2.c
    expect_inode_cache 0 1 1 test1.c
    expect_inode_cache 1 0 0 test2.c

    # -------------------------------------------------------------------------
    TEST "Soft link"

    echo "// soft linked" > test1.c
    ln -fs test1.c test2.c
    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test2.c
    expect_inode_cache 0 1 1 test1.c
    expect_inode_cache 1 0 0 test2.c

    # -------------------------------------------------------------------------
    TEST "Replace"

    echo "// replace" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c

    rm test1.c
    echo "// replace" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c
}
