add_fake_files_counters() {
    local files=$1
    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        mkdir -p $CCACHE_DIR/$x
        echo "0 0 0 0 0 0 0 0 0 0 0 $((files / 16))" >$CCACHE_DIR/$x/stats
    done
}

expect_on_level() {
    local expected_level="$1"

    local files=(
        $(find_result_files "${CCACHE_DIR}")
        $(find_manifest_files "${CCACHE_DIR}")
    )
    for file in "${files[@]}"; do
        slashes=$(echo $file | sed -E -e 's!.*\.ccache/!!' -e 's![^/]*$!!' -e 's![^/]!!g')
        actual_level=$(echo -n "$slashes" | wc -c)
        if [ "$actual_level" -ne "$expected_level" ]; then
            test_failed "file on level $actual_level, expected level $expected_level: $file"
        fi
    done
}


SUITE_cache_levels_SETUP() {
    generate_code 1 test1.c
    unset CCACHE_NODIRECT
}

SUITE_cache_levels() {
    # -------------------------------------------------------------------------
    TEST "Empty cache"

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_on_level 2

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_on_level 2

    # -------------------------------------------------------------------------
    TEST "Many files but still level 2"

    files=$((16 * 16 * 1999))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 2

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 2

    # -------------------------------------------------------------------------
    TEST "Level 3"

    files=$((16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 3

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 3

    # -------------------------------------------------------------------------
    TEST "Level 4"

    files=$((16 * 16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 4

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 4

    # -------------------------------------------------------------------------
    TEST "No deeper than 4 levels"

    files=$((16 * 16 * 16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 4

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache $((files + 2))
    expect_on_level 4
}
