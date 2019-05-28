prepare_cleanup_test_dir() {
    local dir=$1

    rm -rf $dir
    mkdir -p $dir
    if $AGGREGATED; then
    for i in $(seq 0 9); do
        printf '%4017s' '' | tr ' ' 'A' >$dir/result$i-4017.result
        backdate $((3 * i + 1)) $dir/result$i-4017.result
    done
    # NUMFILES: 10, TOTALSIZE: 13 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 10 13 0 0" >$dir/stats
    else
    for i in $(seq 0 9); do
        printf '%4017s' '' | tr ' ' 'A' >$dir/result$i-4017.o
        backdate $((3 * i + 1)) $dir/result$i-4017.o
        backdate $((3 * i + 2)) $dir/result$i-4017.d
        backdate $((3 * i + 3)) $dir/result$i-4017.stderr
    done
    # NUMFILES: 30, TOTALSIZE: 40 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 30 40 0 0" >$dir/stats
    fi
}

SUITE_cleanup() {
    # -------------------------------------------------------------------------
    TEST "Clear cache"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -C >/dev/null
    if $AGGREGATED; then
    expect_file_count 0 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 0
    else
    expect_file_count 0 '*.o' $CCACHE_DIR
    expect_file_count 0 '*.d' $CCACHE_DIR
    expect_file_count 0 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 0
    fi
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, no limits"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    if $AGGREGATED; then
    expect_file_count 10 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 10
    else
    expect_file_count 10 '*.o' $CCACHE_DIR
    expect_file_count 10 '*.d' $CCACHE_DIR
    expect_file_count 10 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 30
    fi
    expect_stat 'cleanups performed' 0

    # -------------------------------------------------------------------------
    TEST "Forced cache cleanup, file limit"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    # No cleanup needed.
    #
    if $AGGREGATED; then
    # 10 * 16 = 160
    $CCACHE -F 160 -M 0 >/dev/null
    else
    # 30 * 16 = 480
    $CCACHE -F 480 -M 0 >/dev/null
    fi
    $CCACHE -c >/dev/null
    if $AGGREGATED; then
    expect_file_count 10 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 10
    else
    expect_file_count 10 '*.o' $CCACHE_DIR
    expect_file_count 10 '*.d' $CCACHE_DIR
    expect_file_count 10 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 30
    fi
    expect_stat 'cleanups performed' 0

    # Reduce file limit
    #
    if $AGGREGATED; then
    # 7 * 16 = 112
    $CCACHE -F 112 -M 0 >/dev/null
    else
    # 22 * 16 = 352
    $CCACHE -F 352 -M 0 >/dev/null
    fi
    $CCACHE -c >/dev/null
    if $AGGREGATED; then
    expect_file_count 7 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 7
    else
    expect_file_count 7 '*.o' $CCACHE_DIR
    expect_file_count 7 '*.d' $CCACHE_DIR
    expect_file_count 8 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 22
    fi
    expect_stat 'cleanups performed' 1
    if $AGGREGATED; then
    for i in 0 1 2; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_missing $CCACHE_DIR/a/result$i-4017.result
    done
    for i in 3 4 5 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_exists $file
    done
    else

    for i in 0 1 2; do
        file=$CCACHE_DIR/a/result$i-4017.o
        expect_file_missing $CCACHE_DIR/a/result$i-4017.o
    done
    for i in 3 4 5 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        expect_file_exists $file
    done
    fi

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
    $CCACHE -c >/dev/null
    if $AGGREGATED; then
    expect_file_count 3 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 3
    else
    expect_file_count 3 '*.o' $CCACHE_DIR
    expect_file_count 4 '*.d' $CCACHE_DIR
    expect_file_count 4 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 11
    fi
    expect_stat 'cleanups performed' 1
    if $AGGREGATED; then
    for i in 0 1 2 3 4 5 6; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_missing $file
    done
    for i in 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.result
        expect_file_exists $file
    done
    else
    for i in 0 1 2 3 4 5 6; do
        file=$CCACHE_DIR/a/result$i-4017.o
        expect_file_missing $file
    done
    for i in 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        expect_file_exists $file
    done
    fi

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.9"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    if $AGGREGATED; then
    $CCACHE -F 160 -M 0 >/dev/null
    else
    $CCACHE -F 480 -M 0 >/dev/null
    fi

    if $AGGREGATED; then
    expect_file_count 160 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 160
    else
    expect_file_count 160 '*.o' $CCACHE_DIR
    expect_file_count 160 '*.d' $CCACHE_DIR
    expect_file_count 160 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 480
    fi
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.9 $CCACHE_COMPILE -c empty.c -o empty.o
    if $AGGREGATED; then
    expect_file_count 159 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 159
    else
    expect_file_count 159 '*.o' $CCACHE_DIR
    expect_file_count 159 '*.d' $CCACHE_DIR
    expect_file_count 159 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 477
    fi
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    TEST "Automatic cache cleanup, limit_multiple 0.7"

    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test_dir $CCACHE_DIR/$x
    done

    if $AGGREGATED; then
    $CCACHE -F 160 -M 0 >/dev/null
    else
    $CCACHE -F 480 -M 0 >/dev/null
    fi

    if $AGGREGATED; then
    expect_file_count 160 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 160
    else
    expect_file_count 160 '*.o' $CCACHE_DIR
    expect_file_count 160 '*.d' $CCACHE_DIR
    expect_file_count 160 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 480
    fi
    expect_stat 'cleanups performed' 0

    touch empty.c
    CCACHE_LIMIT_MULTIPLE=0.7 $CCACHE_COMPILE -c empty.c -o empty.o
    if $AGGREGATED; then
    expect_file_count 157 '*.result' $CCACHE_DIR
    expect_stat 'files in cache' 157
    else
    expect_file_count 157 '*.o' $CCACHE_DIR
    expect_file_count 157 '*.d' $CCACHE_DIR
    expect_file_count 157 '*.stderr' $CCACHE_DIR
    expect_stat 'files in cache' 471
    fi
    expect_stat 'cleanups performed' 1

    # -------------------------------------------------------------------------
    if ! $AGGREGATED; then
    TEST ".o file is removed before .stderr"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    $CCACHE -F 464 -M 0 >/dev/null
    backdate 0 $CCACHE_DIR/a/result9-4017.stderr
    $CCACHE -c >/dev/null
    expect_file_missing $CCACHE_DIR/a/result9-4017.stderr
    expect_file_missing $CCACHE_DIR/a/result9-4017.o

    # Counters expectedly doesn't match reality if x.stderr is found before
    # x.o and the cleanup stops before x.o is found.
    expect_stat 'files in cache' 29
    expect_file_count 28 '*.*' $CCACHE_DIR/a
    fi

    # -------------------------------------------------------------------------
    if ! $AGGREGATED; then
    TEST ".stderr file is not removed before .o"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    $CCACHE -F 464 -M 0 >/dev/null
    backdate 0 $CCACHE_DIR/a/result9-4017.o
    $CCACHE -c >/dev/null
    expect_file_exists $CCACHE_DIR/a/result9-4017.stderr
    expect_file_missing $CCACHE_DIR/a/result9-4017.o

    expect_stat 'files in cache' 29
    expect_file_count 29 '*.*' $CCACHE_DIR/a
    fi

    # -------------------------------------------------------------------------
    TEST "No cleanup of new unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a

    touch $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    if $AGGREGATED; then
    expect_stat 'files in cache' 11
    else
    expect_stat 'files in cache' 31
    fi

    if $AGGREGATED; then
    $CCACHE -F 160 -M 0 >/dev/null
    else
    $CCACHE -F 480 -M 0 >/dev/null
    fi
    $CCACHE -c >/dev/null
    expect_file_exists $CCACHE_DIR/a/abcd.unknown
    if $AGGREGATED; then
    expect_stat 'files in cache' 10
    else
    expect_stat 'files in cache' 30
    fi

    # -------------------------------------------------------------------------
    TEST "Cleanup of old unknown file"

    prepare_cleanup_test_dir $CCACHE_DIR/a
    if $AGGREGATED; then
    $CCACHE -F 160 -M 0 >/dev/null
    else
    $CCACHE -F 480 -M 0 >/dev/null
    fi
    touch $CCACHE_DIR/a/abcd.unknown
    backdate $CCACHE_DIR/a/abcd.unknown
    $CCACHE -F 0 -M 0 -c >/dev/null # update counters
    if $AGGREGATED; then
    expect_stat 'files in cache' 11
    else
    expect_stat 'files in cache' 31
    fi

    if $AGGREGATED; then
    $CCACHE -F 160 -M 0 -c >/dev/null
    else
    $CCACHE -F 480 -M 0 -c >/dev/null
    fi
    expect_file_missing $CCACHE_DIR/a/abcd.unknown
    if $AGGREGATED; then
    expect_stat 'files in cache' 10
    else
    expect_stat 'files in cache' 30
    fi

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
    if $AGGREGATED; then
    expect_stat 'files in cache' 10
    else
    expect_stat 'files in cache' 30
    fi
}
