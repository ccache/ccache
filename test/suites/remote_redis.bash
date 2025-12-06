REDIS_SERVER=$(command -v redis-server || command -v valkey-server)
REDIS_CLI=$(command -v redis-cli || command -v valkey-cli)

SUITE_remote_redis_PROBE() {
    if ! $CCACHE --version | grep -Fq -- redis-storage &> /dev/null; then
        echo "redis-storage not available"
        return
    fi
    if [ -z "${REDIS_SERVER}" ]; then
        echo "neither redis-server nor valkey-server found"
        return
    fi
    if [ -z "${REDIS_CLI}" ]; then
        echo "neither redis-cli nor valkey-cli found"
        return
    fi
}

start_redis_server() {
    local port="$1"
    local password="${2:-}"

    ${REDIS_SERVER} --bind localhost --port "${port}" >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! ${REDIS_CLI} -p "${port}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ -n "${password}" ]; then
        ${REDIS_CLI} -p "${port}" config set requirepass "${password}" &>/dev/null
    fi
}

SUITE_remote_redis_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_redis_cache_entries() {
    local expected=$1
    local url=$2
    local actual

    actual=$(${REDIS_CLI} -u "$url" keys "ccache:*" 2>/dev/null | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_remote_redis() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    port=7777
    redis_url="redis://localhost:${port}"
    export CCACHE_REMOTE_STORAGE="${redis_url}"

    start_redis_server "${port}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Password"

    port=7777
    password=secret123
    redis_url="redis://${password}@localhost:${port}"
    export CCACHE_REMOTE_STORAGE="${redis_url}"

    start_redis_server "${port}" "${password}"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest
    expect_not_contains test.o.*.ccache-log "${password}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Unreachable server"

    export CCACHE_REMOTE_STORAGE="redis://localhost:1"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_error 1
}
