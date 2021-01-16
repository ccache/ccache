prepare_cleanup_test_dir() {
    local dir=$1
    local i

    rm -rf $dir
    mkdir -p $dir
    for ((i = 0; i < 10; ++i)); do
        printf 'A%.0s' {1..4017} >$dir/result${i}R
        backdate $((3 * i + 1)) $dir/result${i}R
    done
    # NUMFILES: 10, TOTALSIZE: 13 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 10 13 0 0" >$dir/stats
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -C >/dev/null
    expect_file_count 0 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 0
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*R' $CCACHE_DIR
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
    expect_file_count 10 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 10
    expect_stat 'cleanups performed' 0

    # Reduce file limit
    #
    # 7 * 16 = 112
    $CCACHE -F 112 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 7 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 7
    expect_stat 'cleanups performed' 1
    for i in 0 1 2; do
        file=$CCACHE_DIR/a/result${i}R
        expect_missing $CCACHE_DIR/a/result${i}R
    done
    for i in 3 4 5 6 7 8 9; do
        file=$CCACHE_DIR/a/result${i}R
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

        prepare_cleanup_test_dir $CCACHE_DIR/a

        $CCACHE -F 0 -M 256K >/dev/null
        $CCACHE -c >/dev/null
        expect_file_count 3 '*R' $CCACHE_DIR
        expect_stat 'files in cache' 3
        expect_stat 'cleanups performed' 1
        for i in 0 1 2 3 4 5 6; do
            file=$CCACHE_DIR/a/result${i}R
            expect_missing $file
        done
        for i in 7 8 9; do
            file=$CCACHE_DIR/a/result${i}R
            expect_exists $file
        done
    fi
    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.9"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 160 -M 0 >/dev/null

    expect_file_count 160 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 160
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.9 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 159 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 159
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.7"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 160 -M 0 >/dev/null

    expect_file_count 160 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 160
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.7 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 157 '*R' $CCACHE_DIR
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
    expect_exists $CCACHE_DIR/a/abcd.unknown
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
    expect_missing $CCACHE_DIR/a/abcd.unknown
    expect_stat 'files in cache' 10

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a
    touch $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat 'files in cache' 1
    backdate $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    expect_missing $CCACHE_DIR/a/abcd.tmp.efgh
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
    TEST "Cleanup of old files by age"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    touch $CCACHE_DIR/a/nowR
    $CCACHE -F 0 -M 0 >/dev/null

    $CCACHE --evict-older-than 1d >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 1

    $CCACHE --evict-older-than 1d  >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat 'files in cache' 1

    backdate $CCACHE_DIR/a/nowR
    $CCACHE --evict-older-than 10s  >/dev/null
    expect_stat 'files in cache' 0
}
