SUITE_nvcc_nocpp2_PROBE() {
    nvcc_PROBE
}

SUITE_nvcc_nocpp2_SETUP() {
    export CCACHE_NOCPP2=1
    nvcc_SETUP
}

SUITE_nvcc_nocpp2() {
    nvcc_tests
}
