SUITE_nocpp2_PROBE() {
    if $HOST_OS_WINDOWS; then
        echo "CCACHE_NOCPP2 does not work correct on Windows"
        return
    fi
}

SUITE_nocpp2_SETUP() {
    export CCACHE_NOCPP2=1
    generate_code 1 test1.c
}

SUITE_nocpp2() {
    base_tests
}
