SUITE_fdiagnostics_format_PROBE() {
    touch test.c
    if ! $COMPILER -c -fdiagnostics-format=text test.c 2>/dev/null; then
        echo "-fdiagnostics-format=text not supported by compiler"
    fi
}

SUITE_fdiagnostics_format_SETUP() {
    mkdir -p src
    mkdir -p obj
    generate_code 1 src/input.a.b
}

SUITE_fdiagnostics_format() {
    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-format=sarif-file unsupported"

    # -fdiagnostics-format=sarif-file is unsupported due to version-dependent default file location.
    $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-file -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-format=text"

    $CCACHE_COMPILE -x c -fdiagnostics-format=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -x c -fdiagnostics-format=text -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-fdiagnostics-format=sarif-stderr"

    if $COMPILER -x c -fdiagnostics-format=sarif-stderr -c src/input.a.b 2>/dev/null; then
        $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-stderr -c src/input.a.b -o obj/output.x.y 2>stderr1.txt
        expect_stat unsupported_compiler_option 0
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1

        $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-stderr -c src/input.a.b -o obj/output.x.y 2>stderr2.txt
        expect_stat unsupported_compiler_option 0
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1
        expect_equal_content stderr1.txt stderr2.txt
    fi
}
