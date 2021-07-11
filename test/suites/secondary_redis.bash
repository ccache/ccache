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

start_redis_server() {
    local port="$1"

    if ! command -v timeout >/dev/null; then
         timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
    fi

    timeout 10 redis-server --bind localhost --port "${port}" >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! redis-cli -p "${port}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done
}

SUITE_secondary_redis_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_redis_cache_entries() {
    local expected=$1
    local url=$2
    local actual

    actual=$(redis-cli -u "$url" keys "ccache:*" | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_secondary_redis() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    port=7777
    redis_url="redis://localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_redis_server "${port}"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Read-only"

    port=7777
    redis_url="redis://localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_redis_server "${port}"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest
}
