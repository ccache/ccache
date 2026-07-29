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
    TEST "Sarif default location -fdiagnostics-format fallback"

    # -fdiagnostics-format=sarif-file is not supported due to version dependent default file location
    $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-file -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    # check the two default locations
    if [ -e output.x.b.sarif ]; then
        expect_exist output.x.b.sarif
    else
        expect_exist obj/output.x.b.sarif
    fi

    # -------------------------------------------------------------------------
    TEST "Sarif default location -fdiagnostics-set-output=sarif fallback"

    # -fdiagnostics-set-output=sarif is not supported due to version dependent default file location
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    # check the two default locations
    if [ -e output.x.b.sarif ]; then
        expect_exist output.x.b.sarif
    else
        expect_exist obj/output.x.b.sarif
    fi

    # -------------------------------------------------------------------------
    TEST "Sarif diagnostics multi set fallback"

    # multiple set is not supported due to documentation implementation mismatch
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:file=output.x.b.sarif -fdiagnostics-set-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    expect_exists output.x.b.sarif

    # -------------------------------------------------------------------------
    TEST "Sarif diagnostics add set fallback"

    # multiple add+set is not supported due to documentation implementation mismatch
    $CCACHE_COMPILE -x c -fdiagnostics-add-output=sarif:file=output.x.b.sarif -fdiagnostics-set-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    expect_exists output.x.b.sarif

    # -------------------------------------------------------------------------
    TEST "Sarif multi add diagnostics shall cache"

    # multiple add is supported
    $CCACHE_COMPILE -x c -fdiagnostics-add-output=sarif:file=output.x.b.sarif -fdiagnostics-add-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif

    rm -f output.x.b.sarif
    # finally test if cache is working
    $CCACHE_COMPILE -x c -fdiagnostics-add-output=sarif:file=output.x.b.sarif -fdiagnostics-add-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif

    # -------------------------------------------------------------------------
    TEST "Sarif set diagnostics shall cache"

    # multiple add is supported
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:file=output.x.b.sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif

    rm -f output.x.b.sarif
    # finally test if cache is working
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:file=output.x.b.sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_exists output.x.b.sarif
}
