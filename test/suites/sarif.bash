SUITE_sarif_PROBE() {
    touch test.c
    if ! $COMPILER -c -fdiagnostics-format=sarif-file test.c 2>/dev/null; then
        echo "-fdiagnostics-format=sarif-file not supported by compiler"
    fi
}

SUITE_sarif_SETUP() {
    generate_code 1 test1.c
}

SUITE_sarif() {
    # -------------------------------------------------------------------------
    TEST "Sarif diagnostics"

    $COMPILER -c -fdiagnostics-format=sarif-file test1.c

    $CCACHE_COMPILE -c -fdiagnostics-format=sarif-file test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists test1.c.sarif

    rm test1.c.sarif

    $CCACHE_COMPILE -c -fdiagnostics-format=sarif-file test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists test1.c.sarif
}
