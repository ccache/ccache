SUITE_stats_log_SETUP() {
    generate_code 1 test.c
    unset CCACHE_NODIRECT
    export CCACHE_STATSLOG=stats.log
}

SUITE_stats_log() {
    # -------------------------------------------------------------------------
    TEST "CCACHE_STATSLOG"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_miss 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_miss 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 2

    expect_content stats.log "# test.c
cache_miss
direct_cache_miss
local_storage_miss
local_storage_miss
preprocessed_cache_miss
# test.c
direct_cache_hit
local_storage_hit
local_storage_hit"
}
