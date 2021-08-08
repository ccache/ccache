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
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    expect_content stats.log "# test.c
cache_miss
# test.c
direct_cache_hit"
}
