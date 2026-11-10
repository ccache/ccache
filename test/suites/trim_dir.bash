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
    TEST "Trim log"

    mkdir -p remote
    filename="entry with spaces"
    dd if=/dev/zero of="remote/$filename" count=1 bs=8192 2>/dev/null
    expect_exists "remote/$filename"
    TZ=UTC0 touch -t 202001020304.05 "remote/$filename"

    TZ=EST5 $CCACHE --trim-dir remote --trim-max-size 1KiB --trim-log trim.log >/dev/null
    expect_missing "remote/$filename"
    sed -e 's/"removed_at":"[^" ]*Z"/"removed_at":"UTC"/' \
        -e 's/\r$//' -e 's,\\\\,/,g' trim.log >normalized.log
    expect_content normalized.log \
        '{"removed_at":"UTC","atime":"2020-01-02T03:04:05Z","mtime":"2020-01-02T03:04:05Z","size":8192,"path":"remote/entry with spaces"}'
    if [ "$(wc -l <trim.log)" -ne 1 ]; then
        test_failed "Expected one JSONL record"
    fi

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

    # -------------------------------------------------------------------------
    TEST "Trim marker file"

    rm -rf remote

    # Populate the remote directory with real, uncompressed cache entries so
    # that recompression changes their size. (Dummy files can't be recompressed
    # and wouldn't trigger a "Recompressed" log.)
    generate_code 1000 marker_test.c
    remote_url=$(file_storage_url "$PWD/remote")
    CCACHE_REMOTE_STORAGE="$remote_url" CCACHE_NOCOMPRESS=1 \
        $CCACHE_COMPILE -c marker_test.c -o marker_test.o

    # Make all entries older than any marker written below so that the marker
    # cutoff can skip them on a subsequent run.
    backdate $(find remote -type f)

    expect_missing marker

    # First run: marker doesn't exist, so recompress all files.
    $CCACHE --trim-dir remote --trim-max-size 0 --trim-recompress 5 \
            --trim-marker marker >out1
    expect_exists marker
    expect_content marker "5"
    expect_contains out1 "Recompressed"

    # Second run with the same level: all files are older than the marker, so
    # recompress none.
    $CCACHE --trim-dir remote --trim-max-size 0 --trim-recompress 5 \
            --trim-marker marker >out2
    expect_contains out2 "No new cache entries to recompress"
    expect_content marker "5"

    # New compression level: recompress all.
    $CCACHE --trim-dir remote --trim-max-size 0 --trim-recompress uncompressed \
            --trim-marker marker >out3
    expect_contains out3 "Recompressed"
    expect_content marker "uncompressed"
}
