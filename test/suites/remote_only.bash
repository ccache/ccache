SUITE_remote_only_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_REMOTE_STORAGE="file:$PWD/remote"
    export CCACHE_REMOTE_ONLY=1

    generate_code 1 test.c
}

SUITE_remote_only() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 0
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 0
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 0
    expect_stat local_storage_write 0
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 0
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 0
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 0
    expect_stat local_storage_write 0
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest
}
