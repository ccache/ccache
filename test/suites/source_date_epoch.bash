SUITE_source_date_epoch_PROBE() {
    echo 'char x[] = __DATE__;' >test.c
    if ! SOURCE_DATE_EPOCH=0 $COMPILER -E test.c | grep -q 1970; then
        echo "SOURCE_DATE_EPOCH not supported by compiler"
    fi
}

SUITE_source_date_epoch_SETUP() {
    echo 'char x;' >without_temporal_macros.c
    echo 'char x[] = __DATE__;' >with_date_macro.c
    echo 'char x[] = __TIME__;' >with_time_macro.c
}

SUITE_source_date_epoch() {
    # -------------------------------------------------------------------------
    TEST "Without temporal macro"

    unset CCACHE_NODIRECT

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c without_temporal_macros.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c without_temporal_macros.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=2 $CCACHE_COMPILE -c without_temporal_macros.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "With __DATE__ macro"

    unset CCACHE_NODIRECT

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_date_macro.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_date_macro.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=2 $CCACHE_COMPILE -c with_date_macro.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "With __TIME__ macro"

    unset CCACHE_NODIRECT

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=2 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "With __TIME__ and time_macros sloppiness"

    unset CCACHE_NODIRECT

    CCACHE_SLOPPINESS=time_macros SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS=time_macros SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS=time_macros SOURCE_DATE_EPOCH=2 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    SOURCE_DATE_EPOCH=1 $CCACHE_COMPILE -c with_time_macro.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
}
