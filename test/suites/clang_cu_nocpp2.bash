SUITE_clang_cu_nocpp2_PROBE() {
    clang_cu_PROBE
}

SUITE_clang_cu_nocpp2_SETUP() {
    export CCACHE_NOCPP2=1

    clang_cu_SETUP
}

SUITE_clang_cu_nocpp2() {
    clang_cu_tests
}
