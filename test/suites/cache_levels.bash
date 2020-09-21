add_fake_files_counters() {
    local files=$1
    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        mkdir -p $CCACHE_DIR/$x
        echo "0 0 0 0 0 0 0 0 0 0 0 $((files / 16))" >$CCACHE_DIR/$x/stats
    done
}

expect_on_level() {
    local type="$1"
    local expected_level="$2"

    slashes=$(find $CCACHE_DIR -name "*$type" \
                  | sed -r -e 's!.*\.ccache/!!' -e 's![^/]*$!!' -e 's![^/]!!g')
    actual_level=$(echo -n "$slashes" | wc -c)
    if [ "$actual_level" -ne "$expected_level" ]; then
        test_failed "$type file on level $actual_level, expected level $expected_level"
    fi
}


SUITE_cache_levels_SETUP() {
    generate_code 1 test1.c
    unset CCACHE_NODIRECT
}

SUITE_cache_levels() {
    # -------------------------------------------------------------------------
    TEST "Empty cache"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_on_level R 2
    expect_on_level M 2

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_on_level R 2
    expect_on_level M 2

    # -------------------------------------------------------------------------
    TEST "Many files but still level 2"

    files=$((16 * 16 * 1999))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' $((files + 2))
    expect_on_level R 2
    expect_on_level M 2

    # -------------------------------------------------------------------------
    TEST "Level 3"

    files=$((16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' $((files + 2))
    expect_on_level R 3
    expect_on_level M 3

    # -------------------------------------------------------------------------
    TEST "Level 4"

    files=$((16 * 16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' $((files + 2))
    expect_on_level R 4
    expect_on_level M 4

    # -------------------------------------------------------------------------
    TEST "No deeper than 4 levels"

    files=$((16 * 16 * 16 * 16 * 2001))
    add_fake_files_counters $files

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' $((files + 2))
    expect_on_level R 4
    expect_on_level M 4
}
