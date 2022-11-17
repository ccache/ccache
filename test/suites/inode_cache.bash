SUITE_inode_cache_PROBE() {
    if $HOST_OS_WINDOWS; then
        echo "inode cache not available on Windows"
        return
    fi

    unset CCACHE_NODIRECT
    export CCACHE_TEMPDIR="${CCACHE_DIR}/tmp"

    touch test.c
    $CCACHE $COMPILER -c test.c
    if [[ ! -f "${CCACHE_TEMPDIR}/inode-cache-32.v1" && ! -f "${CCACHE_TEMPDIR}/inode-cache-64.v1" ]]; then
        local fs_type=$(stat -fLc %T "${CCACHE_DIR}")
        echo "inode cache not supported on ${fs_type}"
    fi
}

SUITE_inode_cache_SETUP() {
    export CCACHE_DEBUG=1
    unset CCACHE_NODIRECT
    export CCACHE_TEMPDIR="${CCACHE_DIR}/tmp"  # isolate inode cache file

    # Disable safety guard against race condition in InodeCache. This is OK
    # since files used in the tests have different sizes and thus will have
    # different cache keys even if ctime/mtime are not updated quickly enough.
    export CCACHE_DISABLE_INODE_CACHE_MIN_AGE=1
}

SUITE_inode_cache() {
    inode_cache_tests
}

expect_inode_cache_type() {
    local expected=$1
    local source_file=$2
    local type=$3

    local actual=$(grep -c "Inode cache $type: $source_file" ${source_file/%.c/.o}.*.ccache-log)
    if [ $actual -ne $expected ]; then
        test_failed_internal "Found $actual (expected $expected) $type for $source_file"
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
    rm *.ccache-*

    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 1 0 0 test1.c

    # -------------------------------------------------------------------------
    TEST "Backdate"

    echo "// backdate" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c
    rm *.ccache-*

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
    TEST "Symbolic link"

    echo "// symbolically linked" > test1.c
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
    rm *.ccache-*

    rm test1.c
    echo "// replace" > test1.c
    $CCACHE_COMPILE -c test1.c
    expect_inode_cache 0 1 1 test1.c
}
