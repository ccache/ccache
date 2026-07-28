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
    TEST "Sarif diagnostics 1"

    # -fdiagnostics-format=sarif-file is not supported due to version dependent default file location
    $CCACHE_COMPILE -x c -fdiagnostics-format=sarif-file -c src/input.a.b -o obj/output.x.y
    expect_stat unsupported_compiler_option 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    # check the two default locations
    if [ -e output.x.b.sarif ] then
        expect_exist output.x.b.sarif
    else
        expect_exist obj/output.x.b.sarif
    fi
}
