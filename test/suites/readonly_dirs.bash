SUITE_readonly_dirs_SETUP() {
    generate_code 1 test.c
}

SUITE_readonly_dirs() {
    # -------------------------------------------------------------------------
    TEST "Cache hit from read-only directory (preprocessed mode)"

    # Populate a separate cache directory.
    CCACHE_DIR=$PWD/fallback $CCACHE_COMPILE -c test.c
    rm test.o

    # The local cache is empty, so the result must come from the read-only
    # directory and be promoted into the local cache.
    CCACHE_READONLY_DIRS=$PWD/fallback $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 0
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 1
    expect_stat local_storage_write 1 # promoted result
    expect_stat files_in_cache 1
    expect_exists test.o

    # The promoted result is now served from the local cache without any
    # read-only directory configured.
    $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "Cache hit from read-only directory (direct mode)"

    unset CCACHE_NODIRECT

    # Populate a separate cache directory (manifest + result).
    CCACHE_DIR=$PWD/fallback $CCACHE_COMPILE -c test.c
    rm test.o

    CCACHE_READONLY_DIRS=$PWD/fallback $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 0
    expect_stat local_storage_write 2 # promoted manifest + result
    expect_stat files_in_cache 2
    expect_exists test.o

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "Cache miss falls back to compiler"

    # The read-only directory does not contain a matching result.
    CCACHE_READONLY_DIRS=$PWD/empty $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists test.o

    # -------------------------------------------------------------------------
    TEST "Multiple read-only directories"

    generate_code 2 test2.c

    CCACHE_DIR=$PWD/fallback1 $CCACHE_COMPILE -c test.c
    CCACHE_DIR=$PWD/fallback2 $CCACHE_COMPILE -c test2.c
    rm test.o test2.o

    CCACHE_READONLY_DIRS="$PWD/fallback1:$PWD/fallback2" $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 0
    expect_exists test.o

    CCACHE_READONLY_DIRS="$PWD/fallback1:$PWD/fallback2" $CCACHE_COMPILE -c test2.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 0
    expect_exists test2.o

    # -------------------------------------------------------------------------
    TEST "Raw files are promoted (hard link mode)"

    # With hard_link enabled the object file is stored as a separate raw file
    # alongside the result entry. Promotion must copy the raw file too,
    # otherwise retrieval from the local cache would fail.
    CCACHE_HARDLINK=1 CCACHE_DIR=$PWD/fallback $CCACHE_COMPILE -c test.c
    rm test.o

    CCACHE_HARDLINK=1 CCACHE_READONLY_DIRS=$PWD/fallback $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 0
    expect_stat files_in_cache 2 # promoted result + raw file
    expect_exists test.o

    # The promoted entry (including the raw file) is served from the local
    # cache without any read-only directory configured.
    rm test.o
    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 0
    expect_exists test.o

    # -------------------------------------------------------------------------
    TEST "Read-only directory is not modified"

    CCACHE_DIR=$PWD/fallback $CCACHE_COMPILE -c test.c
    rm test.o
    files_before=$(find $PWD/fallback -type f | wc -l)

    CCACHE_READONLY_DIRS=$PWD/fallback $CCACHE_COMPILE -c test.c
    expect_stat preprocessed_cache_hit 1
    files_after=$(find $PWD/fallback -type f | wc -l)

    if [ "$files_after" -ne "$files_before" ]; then
        test_failed "Read-only directory was modified ($files_before -> $files_after files)"
    fi
}
