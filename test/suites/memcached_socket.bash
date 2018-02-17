SUITE_memcached_socket_PROBE() {
    probe_memcached
}

SUITE_memcached_socket_SETUP() {
    export CCACHE_MEMCACHED_CONF="--SOCKET=\"/tmp/memcached.$$\""

    generate_code 1 test1.c
    start_memcached -s /tmp/memcached.$$
}

SUITE_memcached_socket() {
    base_tests
    rm /tmp/memcached.$$
}
