SUITE_secondary_file_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_SECONDARY_STORAGE="file:$PWD/secondary"

    generate_code 1 test.c
}

SUITE_secondary_file() {
    # -------------------------------------------------------------------------
    TEST "Subdirs layout"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_exists secondary/CACHEDIR.TAG
    subdirs=$(find secondary -type d | wc -l)
    if [ "${subdirs}" -lt 2 ]; then # "secondary" itself counts as one
        test_failed "Expected subdirectories in secondary"
    fi
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Flat layout"

    CCACHE_SECONDARY_STORAGE+="|layout=flat"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_exists secondary/CACHEDIR.TAG
    subdirs=$(find secondary -type d | wc -l)
    if [ "${subdirs}" -ne 1 ]; then # "secondary" itself counts as one
        test_failed "Expected no subdirectories in secondary"
    fi
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Two directories"

    CCACHE_SECONDARY_STORAGE+=" file://$PWD/secondary_2"
    mkdir secondary_2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    rm -r secondary/??
    expect_file_count 1 '*' secondary # CACHEDIR.TAG

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary_2
    expect_file_count 1 '*' secondary # CACHEDIR.TAG
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "umask"

    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary|umask=022"
    rm -rf secondary
    $CCACHE_COMPILE -c test.c
    expect_perm secondary drwxr-xr-x
    expect_perm secondary/CACHEDIR.TAG -rw-r--r--

    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary|umask=000"
    $CCACHE -C >/dev/null
    rm -rf secondary
    $CCACHE_COMPILE -c test.c
    expect_perm secondary drwxrwxrwx
    expect_perm secondary/CACHEDIR.TAG -rw-rw-rw-

    # -------------------------------------------------------------------------
    TEST "Sharding"

    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary/*|shards=a,b(2)"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    if [ ! -d secondary/a ] && [ ! -d secondary/b ]; then
        test_failed "Expected secondary/a or secondary/b to exist"
    fi
}
