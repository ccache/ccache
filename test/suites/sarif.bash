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

    $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-file -c src/input.a.b -o obj/output.x.y
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif

    rm output.x.b.sarif

    $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-file -c src/input.a.b -o obj/output.x.y
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif
}
