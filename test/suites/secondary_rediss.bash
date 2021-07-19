SUITE_secondary_rediss_PROBE() {
    if ! $CCACHE --version | fgrep -q -- redis-storage &> /dev/null; then
        echo "redis-storage not available"
        return
    fi
    if ! command -v redis-server &> /dev/null; then
        echo "redis-server not found"
        return
    fi
    if ! command -v redis-cli &> /dev/null; then
        echo "redis-cli not found"
        return
    fi
}

start_rediss_server() {
    local port="$1"
    local password="${2:-}"
    redis-server --bind localhost --tls-port "${port}" --port 0 >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! redis-cli --tls -p "${port}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ -n "${password}" ]; then
        redis-cli --tls -p "${port}" config set requirepass "${password}" &>/dev/null
    fi
}

SUITE_secondary_rediss_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_rediss_cache_entries() {
    local expected=$1
    local url=${2/rediss/redis}  # use --tls parameter instead of url ("unknown scheme")
    local actual

    actual=$(redis-cli --tls -u "$url" keys "ccache:*" 2>/dev/null | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_secondary_rediss() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    port=7777
    redis_url="rediss://localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_rediss_server "${port}"
    function expect_number_of_redis_cache_entries()
    {
       expect_number_of_rediss_cache_entries "$@"
    }

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
    TEST "Password"

    port=7777
    password=secret
    redis_url="rediss://${password}@localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_rediss_server "${port}" "${password}"
    function expect_number_of_redis_cache_entries()
    {
       expect_number_of_rediss_cache_entries "$@"
    }

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
}
