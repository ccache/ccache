SUITE_memcached_only_SETUP() {
    generate_code 1 test1.c
}

SUITE_memcached_only() {
    CCACHE_NOFILES=true
    export CCACHE_MEMCACHED_CONF=--SERVER=localhost:22122
    export CCACHE_MEMCACHED_ONLY=1
    memcached -p 22122 &
    memcached_pid=$!
    base_tests
    kill $memcached_pid
    unset CCACHE_MEMCACHED_CONF
    unset CCACHE_MEMCACHED_ONLY
    unset CCACHE_NOFILES
}

