prepare_cleanup_test_dir() {
    local dir=$1

    rm -rf $dir
    mkdir -p $dir
    for i in $(seq 0 9); do
        printf '%4017s' '' | tr ' ' 'A' >$dir/result$i-4017.o
        touch $dir/result$i-4017.stderr
        touch $dir/result$i-4017.d
        if [ $i -gt 5 ]; then
            backdate $dir/result$i-4017.stderr
        fi
    done
    # NUMFILES: 30, TOTALSIZE: 40 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 30 40 0 0" >$dir/stats
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -C >/dev/null
    expect_file_count 0 '*.o' $CCACHE_DIR
    expect_file_count 0 '*.d' $CCACHE_DIR
    expect_file_count 0 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 0
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*.o' $CCACHE_DIR
    expect_file_count 10 '*.d' $CCACHE_DIR
    expect_file_count 10 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 30
    expect_stat 'cleanups performed' 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # No cleanup needed.
    #
    # 30 * 16 = 480
    $CCACHE -F 480 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 10 '*.o' $CCACHE_DIR
    expect_file_count 10 '*.d' $CCACHE_DIR
    expect_file_count 10 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 30
    expect_stat 'cleanups performed' 0

    # Reduce file limit
    #
    # 21 * 16 = 336
    $CCACHE -F 336 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 7 '*.o' $CCACHE_DIR
    expect_file_count 7 '*.d' $CCACHE_DIR
    expect_file_count 7 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 21
    expect_stat 'cleanups performed' 1
    for i in 0 1 2 3 4 5 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 6 7 8; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, size limit"

    # NOTE: This test is known to fail on filesystems that have unusual block
    # sizes, including ecryptfs. The workaround is to place the test directory
    # elsewhere:
    #
    #     cd /tmp
    #     CCACHE=$DIR/ccache $DIR/test.sh

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 256K >/dev/null
    CCACHE_LOGFILE=/tmp/foo $CCACHE -c >/dev/null
    expect_file_count 3 '*.o' $CCACHE_DIR
    expect_file_count 3 '*.d' $CCACHE_DIR
    expect_file_count 3 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 9
    expect_stat 'cleanups performed' 1
    for i in 3 4 5; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 0 1 2 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.9"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 480 -M 0 >/dev/null

    expect_file_count 160 '*.o' $CCACHE_DIR
    expect_file_count 160 '*.d' $CCACHE_DIR
    expect_file_count 160 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 480
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.9 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 159 '*.o' $CCACHE_DIR
    expect_file_count 158 '*.d' $CCACHE_DIR
    expect_file_count 158 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 475
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.7"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    $CCACHE -F 480 -M 0 >/dev/null

    expect_file_count 160 '*.o' $CCACHE_DIR
    expect_file_count 160 '*.d' $CCACHE_DIR
    expect_file_count 160 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 480
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.7 $CCACHE_COMPILE -c empty.c -o empty.o
    expect_file_count 157 '*.o' $CCACHE_DIR
    expect_file_count 156 '*.d' $CCACHE_DIR
    expect_file_count 156 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 469
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Cleanup of sibling files"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 336 -M 0 >/dev/null
    backdate $CCACHE_DIR/a/result2-4017.stderr
    $CCACHE -c >/dev/null
    # floor(0.8 * 9) = 7
    expect_file_count 7 '*.o' $CCACHE_DIR
    expect_file_count 7 '*.d' $CCACHE_DIR
    expect_file_count 7 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 21
    for i in 0 1 3 4 5 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 2 6 7; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "No cleanup of new unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat 'files in cache' 31

    $CCACHE -F 480 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    if [ ! -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown removed"
    fi
    expect_stat 'files in cache' 28

    # -------------------------------------------------------------------------
    TEST "Cleanup of old unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    $CCACHE -F 480 -M 0 >/dev/null
    touch $CCACHE_DIR/a/abcd.unknown
    backdate $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    expect_stat 'files in cache' 31

    $CCACHE -F 480 -M 0 -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown not removed"
    fi
    expect_stat 'files in cache' 30

    # -------------------------------------------------------------------------
    TEST "Cleanup of tmp file"

    mkdir -p $CCACHE_DIR/a
    touch $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    expect_stat 'files in cache' 1
    backdate $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.tmp.efgh ]; then
        test_failed "$CCACHE_DIR/a/abcd.tmp.unknown not removed"
    fi
    expect_stat 'files in cache' 0

    # -------------------------------------------------------------------------
    TEST "No cleanup of .nfs* files"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/.nfs0123456789
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    expect_file_count 1 '.nfs*' $CCACHE_DIR
    expect_stat 'files in cache' 30
}
