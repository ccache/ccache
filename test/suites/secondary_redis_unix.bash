SUITE_secondary_redis_unix_PROBE() {
    if ! $CCACHE --version | fgrep -q -- redis-storage &> /dev/null; then
        echo "redis-storage not available"
        return
    fi
    if ! command -v redis-server &> /dev/null; then
        echo "redis-server not found"
        return
    fi
    if redis-server --unixsocket /foo/redis.sock 2>&1 | grep -q "FATAL CONFIG FILE ERROR"; then
        # "Bad directive or wrong number of arguments"
        echo "redis-server without unixsocket"
        return
    fi
    if ! command -v redis-cli &> /dev/null; then
        echo "redis-cli not found"
        return
    fi
    if ! redis-cli -s /foo/redis.sock --version &> /dev/null; then
        # "Unrecognized option or bad number of args"
        echo "redis-cli without socket"
        return
    fi
}

start_redis_unix_server() {
    local socket="$1"
    local password="${2:-}"

    redis-server --bind localhost --unixsocket "${socket}" --port 0 >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! redis-cli -s "${socket}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ -n "${password}" ]; then
        redis-cli -s "${socket}" config set requirepass "${password}" &>/dev/null
    fi
}

SUITE_secondary_redis_unix_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_redis_unix_cache_entries() {
    local expected=$1
    local socket=$2
    local actual

    actual=$(redis-cli -s "$socket" keys "ccache:*" 2>/dev/null | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $socket"
    fi
}

SUITE_secondary_redis_unix() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    socket=$(mktemp)
    redis_url="redis+unix:${socket}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_redis_unix_server "${socket}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Password"

    socket=$(mktemp)
    password=secret123
    redis_url="redis+unix://${password}@localhost${socket}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}"

    start_redis_unix_server "${socket}" "${password}"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest
    expect_not_contains test.o.*.ccache-log "${password}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Unreachable server"

    export CCACHE_SECONDARY_STORAGE="redis+unix:///foo"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat secondary_storage_error 1
}
