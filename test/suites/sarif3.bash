SUITE_sarif_PROBE() {
    touch test.c
    if ! $COMPILER -c -fdiagnostics-format=sarif-file test.c 2>/dev/null; then
        echo "-fdiagnostics-format=sarif-file not supported by compiler"
    fi
}

SUITE_sarif_SETUP() {
    mkdir -p src
    mkdir -p obj
    generate_code 1 src/input.a.b
}

SUITE_sarif() {
    # -------------------------------------------------------------------------
    TEST "Sarif diagnostics 3"

    # multiple set is not supported due to documentation implementation mismatch
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:file=output.x.b.sarif -fdiagnostics-set-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    expect_exists output.x.b.sarif
}
