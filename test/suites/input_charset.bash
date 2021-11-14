SUITE_input_charset_PROBE() {
    touch test.c
    if ! $COMPILER -c -finput-charset=latin1 test.c >/dev/null 2>&1; then
        echo "compiler doesn't support -finput-charset"
    fi
}

SUITE_input_charset() {
    # -------------------------------------------------------------------------
    TEST "-finput-charset"

    printf '#include <wchar.h>\nwchar_t foo[] = L"\xbf";\n' >latin1.c

    $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    CCACHE_NOCPP2=1 $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NOCPP2=1 $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
}
