SUITE_memcached_PROBE() {
    probe_memcached
}

SUITE_memcached_SETUP() {
    export CCACHE_MEMCACHED_CONF=--SERVER=localhost:22122

    generate_code 1 test1.c
    start_memcached -p 22122
}

SUITE_memcached() {
    base_tests
}
