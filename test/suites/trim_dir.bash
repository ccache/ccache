SUITE_trim_dir_PROBE() {
    if [ -z "$ENABLE_CACHE_CLEANUP_TESTS" ]; then
        echo "cleanup tests disabled"
    fi
}

SUITE_trim_dir() {
    # -------------------------------------------------------------------------
    TEST "Trim remote cache directory"

    if $HOST_OS_APPLE; then
        one_mb=1m
    else
        one_mb=1M
    fi
    for subdir in aa bb cc; do
        mkdir -p remote/$subdir
        dd if=/dev/zero of=remote/$subdir/1 count=1 bs=$one_mb 2>/dev/null
        dd if=/dev/zero of=remote/$subdir/2 count=1 bs=$one_mb 2>/dev/null
    done

    backdate remote/bb/2 remote/cc/1
    $CCACHE --trim-dir remote --trim-max-size 4.5M --trim-method mtime \
            >/dev/null

    expect_exists remote/aa/1
    expect_exists remote/aa/2
    expect_exists remote/bb/1
    expect_missing remote/bb/2
    expect_missing remote/cc/1
    expect_exists remote/cc/2

    # -------------------------------------------------------------------------
    TEST "Trim local cache directory"

    mkdir -p local/0
    touch local/0/stats
    if $CCACHE --trim-dir local --trim-max-size 0 &>/dev/null; then
        test_failed "Expected failure"
    fi

    rm -rf local
    mkdir local
    touch local/ccache.conf
    if $CCACHE --trim-dir local --trim-max-size 0 &>/dev/null; then
        test_failed "Expected failure"
    fi
}
