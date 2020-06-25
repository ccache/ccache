prepare_cleanup_test_dir() {
    local dir=$1

    rm -rf $dir
    mkdir -p $dir
    for i in $(seq 0 9); do
        printf '%4017s' '' | tr ' ' 'A' >$dir/result$i-4017.result
        backdate $((3 * i + 1)) $dir/result$i-4017.result
    done
    # NUMFILES: 10, TOTALSIZE: 13 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 10 13 0 0" >$dir/stats
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -C >/dev/null
    expect_file_count 0 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 0
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 10
    expect_stat 'cleanups performed' 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # No cleanup needed.
    #
    # 10 * 16 = 160
    $CCACHE -F 160 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 10
    expect_stat 'cleanups performed' 0

    # Reduce file limit
    #
    # 7 * 16 = 112
    $CCACHE -F 112 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 7 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 7
    expect_stat 'cleanups performed' 1
    for i in 0 1 2; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_missing $CCACHE_DIR/a/result$i-4017.result
    done
    for i in 3 4 5 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_exists $file
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

        prepare_cleanup_test_dir $CCACHE_DIR/a

        $CCACHE -F 0 -M 256K >/dev/null
        $CCACHE -c >/dev/null
        expect_file_count 3 '*.result' $CCACHE_DIR
        expect_stat 'files in cache' 3
        expect_stat 'cleanups performed' 1
        for i in 0 1 2 3 4 5 6; do
            file=$CCACHE_DIR/a/result$i-4017.result
            expect_file_missing $file
        done
        for i in 7 8 9; do
            file=$CCACHE_DIR/a/result$i-4017.result
            expect_file_exists $file
        done
    fi
    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.9"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 160 -M 0 >/dev/null

    expect_file_count 160 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 160
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.9 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 159 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 159
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.7"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 160 -M 0 >/dev/null

    expect_file_count 160 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 160
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.7 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 157 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 157
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "No cleanup of new unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat 'files in cache' 11

    $CCACHE -F 160 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_exists $CCACHE_DIR/a/abcd.unknown
    expect_stat 'files in cache' 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of old unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    $CCACHE -F 160 -M 0 >/dev/null
    touch $CCACHE_DIR/a/abcd.unknown
    backdate $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat 'files in cache' 11

    $CCACHE -F 160 -M 0 -c >/dev/null
    expect_file_missing $CCACHE_DIR/a/abcd.unknown
    expect_stat 'files in cache' 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a
    touch $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat 'files in cache' 1
    backdate $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    expect_file_missing $CCACHE_DIR/a/abcd.tmp.efgh
    expect_stat 'files in cache' 0

    # -------------------------------------------------------------------------
    TEST "No cleanup of .nfs* files"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/.nfs0123456789
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 1 '.nfs*' $CCACHE_DIR
    expect_stat 'files in cache' 10
    # -------------------------------------------------------------------------
    TEST "cleanup of files older than n seconds"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/now.result
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE --evict-older-than 10 >/dev/null
    expect_file_count 1 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 1
}
