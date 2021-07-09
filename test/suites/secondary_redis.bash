SUITE_secondary_redis_PROBE() {
    if ! command -v redis-server &> /dev/null; then
        echo "redis-server not found"
        return
    fi
    if ! command -v redis-cli &> /dev/null; then
        echo "redis-cli not found"
        return
    fi
}

SUITE_secondary_redis_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_SECONDARY_STORAGE="redis://localhost:7777"

    generate_code 1 test.c
}

expect_number_of_cache_entries() {
    local expected=$1
    local host=$2
    local port=$3
    local actual

    actual=$(redis-cli -h "$host" -p "$port" keys "ccache:*" | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $host:$port"
    fi
}

SUITE_secondary_redis() {
    if $HOST_OS_APPLE; then
         # no coreutils on darwin by default, perl rather than gtimeout
         function timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
    fi
    timeout 10 redis-server --bind localhost --port 7777 &
    sleep 0.1 # wait for boot
    redis-cli -p 7777 ping

    # -------------------------------------------------------------------------
    TEST "Base case"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    redis-cli -p 7777 flushdb

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 localhost 7777 # result + manifest
}
