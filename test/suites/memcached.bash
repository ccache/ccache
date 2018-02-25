SUITE_memcached_SETUP() {
    generate_code 1 test1.c
}

SUITE_memcached() {
    export CCACHE_MEMCACHED_CONF=--SERVER=localhost:22122
    memcached -p 22122 &
    memcached_pid=$!
    base_tests
    kill $memcached_pid
    unset CCACHE_MEMCACHED_CONF
}

