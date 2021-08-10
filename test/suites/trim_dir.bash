SUITE_trim_dir() {
    # -------------------------------------------------------------------------
    TEST "Trim secondary cache directory"

    if $HOST_OS_APPLE; then
        one_mb=1m
    else
        one_mb=1M
    fi
    for subdir in aa bb cc; do
        mkdir -p secondary/$subdir
        dd if=/dev/zero of=secondary/$subdir/1 count=1 bs=$one_mb 2>/dev/null
        dd if=/dev/zero of=secondary/$subdir/2 count=1 bs=$one_mb 2>/dev/null
    done

    backdate secondary/bb/2 secondary/cc/1
    $CCACHE --trim-dir secondary --trim-max-size 4.5M --trim-method mtime \
            >/dev/null

    expect_exists secondary/aa/1
    expect_exists secondary/aa/2
    expect_exists secondary/bb/1
    expect_missing secondary/bb/2
    expect_missing secondary/cc/1
    expect_exists secondary/cc/2

    # -------------------------------------------------------------------------
    TEST "Trim primary cache directory"

    mkdir -p primary/0
    touch primary/0/stats
    if $CCACHE --trim-dir primary --trim-max-size 0 &>/dev/null; then
        test_failed "Expected failure"
    fi

    rm -rf primary
    mkdir primary
    touch primary/ccache.conf
    if $CCACHE --trim-dir primary --trim-max-size 0 &>/dev/null; then
        test_failed "Expected failure"
    fi
}
