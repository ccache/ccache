SUITE_cleanup_PROBE() {
    # NOTE: This test suite is known to fail on filesystems that have unusual
    # block sizes, including ecryptfs. The workaround is to place the test
    # directory elsewhere:
    #
    #     cd /tmp
    #     CCACHE=$DIR/ccache $DIR/test.sh
    if [ -z "$ENABLE_CACHE_CLEANUP_TESTS" ]; then
        echo "ENABLE_CACHE_CLEANUP_TESTS is not set"
    fi
}

SUITE_cleanup_SETUP() {
    mkdir -p $CCACHE_DIR/0/0
    printf 'A%.0s' {1..4017} >"$CCACHE_DIR/0/0/result0R"
    backdate "$CCACHE_DIR/0/0/result0R"
    for ((i = 1; i < 10; ++i )); do
        cp -a "$CCACHE_DIR/0/0/result0R" "$CCACHE_DIR/0/0/result${i}R"
    done

    subdirs=(1 2 3 4 5 6 7 8 9 a b c d e f)
    for c in "${subdirs[@]}"; do
        cp -a "$CCACHE_DIR/0/0" "$CCACHE_DIR/0/${c}"
    done

    for c in "${subdirs[@]}"; do
        cp -a "$CCACHE_DIR/0" "$CCACHE_DIR/${c}"
    done

    $CCACHE -c >/dev/null

    # We have now created 16 * 16 * 10 = 2560 files, each 4017 bytes big (4096
    # bytes on disk), totalling (counting disk blocks) 2560 * 4096 = 10 MiB =
    # 10240 KiB.
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    expect_stat cleanups_performed 0
    $CCACHE -C >/dev/null
    expect_file_count 0 '*R' $CCACHE_DIR
    expect_stat files_in_cache 0
    expect_stat cleanups_performed 256

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no size limit"

    $CCACHE -M 0 -c >/dev/null
    expect_file_count 2560 '*R' $CCACHE_DIR
    expect_stat files_in_cache 2560
    expect_stat cleanups_performed 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    $CCACHE -F 2543 -c >/dev/null

    expect_file_count 2543 '*R' $CCACHE_DIR
    expect_stat files_in_cache 2543
    expect_stat cleanups_performed 17

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, size limit"

    # 10240 KiB - 10230 KiB = 10 KiB, so we need to remove 3 files of 4 KiB byte
    # to get under the limit. Each cleanup only removes one file since there are
    # only 10 files in each directory, so there are 3 cleanups.
    $CCACHE -M 10230KiB -c >/dev/null

    expect_file_count 2557 '*R' $CCACHE_DIR
    expect_stat files_in_cache 2557
    expect_stat cleanups_performed 3

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, file limit"

    $CCACHE -F 2543 >/dev/null

    touch test.c
    $CCACHE_COMPILE -c test.c
    expect_stat files_in_cache 2559
    expect_stat cleanups_performed 1

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, size limit"

    $CCACHE -M 10230KiB >/dev/null

    # Automatic cleanup triggers one cleanup. The directory where the result
    # ended up will have 11 files and will be trimmed down to floor(0.9 * 2561 /
    # 256) = 9 files.

    touch test.c
    $CCACHE_COMPILE -c test.c
    expect_stat files_in_cache 2559
    expect_stat cleanups_performed 1

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a/a
    touch $CCACHE_DIR/a/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat files_in_cache 2561

    backdate $CCACHE_DIR/a/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    expect_missing $CCACHE_DIR/a/a/abcd.tmp.efgh
    expect_stat files_in_cache 2560

    # -------------------------------------------------------------------------
    TEST "No cleanup of .nfs* files"

    mkdir -p $CCACHE_DIR/a/a
    touch $CCACHE_DIR/a/a/.nfs0123456789
    $CCACHE -c >/dev/null
    expect_file_count 1 '.nfs*' $CCACHE_DIR
    expect_stat files_in_cache 2560

    # -------------------------------------------------------------------------
    TEST "Cleanup of old files by age"

    mkdir -p $CCACHE_DIR/a/a
    touch $CCACHE_DIR/a/a/nowR

    $CCACHE --evict-older-than 1d >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat files_in_cache 1

    $CCACHE --evict-older-than 1d  >/dev/null
    expect_file_count 1 '*R' $CCACHE_DIR
    expect_stat files_in_cache 1

    backdate $CCACHE_DIR/a/a/nowR
    $CCACHE --evict-older-than 10s  >/dev/null
    expect_stat files_in_cache 0
}
