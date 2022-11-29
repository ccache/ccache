SUITE_cleanup_SETUP() {
    local l1_dir=$CCACHE_DIR/a
    local l2_dir=$l1_dir/b
    local i

    rm -rf $l2_dir
    mkdir -p $l2_dir
    for ((i = 0; i < 10; ++i)); do
        printf 'A%.0s' {1..4017} >$l2_dir/result${i}R
        backdate $((3 * i + 1)) $l2_dir/result${i}R
    done
    # NUMFILES: 10, TOTALSIZE: 13 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 10 13 0 0" >$l1_dir/stats
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    $CCACHE -C >/dev/null
    expect_file_count 0 '*R' $CCACHE_DIR
    expect_stat files_in_cache 0
    expect_stat cleanups_performed 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*R' $CCACHE_DIR
    expect_stat files_in_cache 10
    expect_stat cleanups_performed 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    # No cleanup needed.
    #
    # 10 * 16 = 160
    $CCACHE -F 160 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*R' $CCACHE_DIR
    expect_stat files_in_cache 10
    expect_stat cleanups_performed 0

    # Reduce file limit
    #
    # 7 * 16 = 112
    $CCACHE -F 112 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 7 '*R' $CCACHE_DIR
    expect_stat files_in_cache 7
    expect_stat cleanups_performed 1
    for i in 0 1 2; do
        file=$CCACHE_DIR/a/b/result${i}R
        expect_missing $CCACHE_DIR/a/result${i}R
    done
    for i in 3 4 5 6 7 8 9; do
        file=$CCACHE_DIR/a/b/result${i}R
        expect_exists $file
    done

    # -------------------------------------------------------------------------
    if [ -n "$ENABLE_CACHE_CLEANUP_TESTS" ]; then
        TEST "Forced cache cleanup, size limit"

        # NOTE: This test is known to fail on filesystems that have unusual block
        # sizes, including ecryptfs. The workaround is to place the test directory
        # elsewhere:
        #
        #     cd /tmp
        #     CCACHE=$DIR/ccache $DIR/test.sh

        $CCACHE -F 0 -M 4096K >/dev/null
        $CCACHE -c >/dev/null
        expect_file_count 3 '*R' $CCACHE_DIR
        expect_stat files_in_cache 3
        expect_stat cleanups_performed 1
        for i in 0 1 2 3 4 5 6; do
            file=$CCACHE_DIR/a/b/result${i}R
            expect_missing $file
        done
        for i in 7 8 9; do
            file=$CCACHE_DIR/a/b/result${i}R
            expect_exists $file
        done
    fi

    # -------------------------------------------------------------------------
    TEST "No cleanup of new unknown file"

    touch $CCACHE_DIR/a/b/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat files_in_cache 11

    $CCACHE -F 160 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_exists $CCACHE_DIR/a/b/abcd.unknown
    expect_stat files_in_cache 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of old unknown file"

    $CCACHE -F 160 -M 0 >/dev/null
    touch $CCACHE_DIR/a/b/abcd.unknown
    backdate $CCACHE_DIR/a/b/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat files_in_cache 11

    $CCACHE -F 160 -M 0 -c >/dev/null
    expect_missing $CCACHE_DIR/a/b/abcd.unknown
    expect_stat files_in_cache 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a
    touch $CCACHE_DIR/a/b/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat files_in_cache 11
    backdate $CCACHE_DIR/a/b/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    expect_missing $CCACHE_DIR/a/b/abcd.tmp.efgh
    expect_stat files_in_cache 10

    # -------------------------------------------------------------------------
    TEST "No cleanup of .nfs* files"

    touch $CCACHE_DIR/a/.nfs0123456789
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 1 '.nfs*' $CCACHE_DIR
    expect_stat files_in_cache 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of old files by age"

    touch $CCACHE_DIR/a/b/nowR
    $CCACHE -F 0 -M 0 >/dev/null

    $CCACHE --evict-older-than 1d >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat files_in_cache 1

    $CCACHE --evict-older-than 1d  >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat files_in_cache 1

    backdate $CCACHE_DIR/a/b/nowR
    $CCACHE --evict-older-than 10s  >/dev/null
    expect_stat files_in_cache 0
}
