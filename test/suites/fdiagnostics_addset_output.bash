SUITE_fdiagnostics_addset_output_PROBE() {
    touch test.c
    if ! $COMPILER -c -fdiagnostics-set-output=sarif test.c 2>/dev/null; then
        echo "-fdiagnostics-set-output=sarif not supported by compiler"
    fi
}

SUITE_fdiagnostics_addset_output_SETUP() {
    mkdir -p src
    mkdir -p obj
    generate_code 1 src/input.a.b
}

SUITE_fdiagnostics_addset_output() {
    # -------------------------------------------------------------------------
    TEST "Default location for -fdiagnostics-set-output=sarif is unsupported"

    # -fdiagnostics-set-output=sarif (without specified file) is unsupported due to version-dependent default file location.
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-format + -fdiagnostics-add-output is unsupported"

    # Too complicated interaction between -fdiagnostics-format and -fdiagnostics-add-output to support for now.
    $CCACHE_COMPILE -x c -fdiagnostics-format=text -fdiagnostics-add-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "Several -fdiagnostics-set-output are unsupported"

    # Multiple -fdiagnostics-set-output unsupported for now.
    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:file=output.x.b.sarif -fdiagnostics-set-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_exists output.x.b.sarif

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-add-output + -fdiagnostics-set-output is unsupported"

    # -fdiagnostics-add-output + -fdiagnostics-set-output unsupported for now.
    $CCACHE_COMPILE -x c -fdiagnostics-add-output=sarif:file=output.x.b.sarif -fdiagnostics-set-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_exists output.x.b.sarif

    # -------------------------------------------------------------------------
    TEST "Multiple -fdiagnostics-add-output is unsupported"

    $CCACHE_COMPILE -x c -fdiagnostics-add-output=sarif:file=output.x.b.sarif -fdiagnostics-add-output=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-set-output=text with key options"

    $CCACHE_COMPILE -x c -fdiagnostics-set-output=text:color=no -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -x c -fdiagnostics-set-output=text:color=no -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-set-output=sarif with key options"

    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:version=2.1,file=output.x.b.sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists output.x.b.sarif

    rm -f output.x.b.sarif

    $CCACHE_COMPILE -x c -fdiagnostics-set-output=sarif:version=2.1,file=output.x.b.sarif -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_exists output.x.b.sarif
}
