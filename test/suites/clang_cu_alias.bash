SUITE_clang_cu_alias_PROBE() {
    clang_cu_PROBE
}

SUITE_clang_cu_alias_SETUP() {
    export CLANG_CU_LANG_TYPE="cuda"

    clang_cu_SETUP
}

SUITE_clang_cu_alias() {
    clang_cu_tests
}
