REDIS_SERVER=$(command -v redis-server || command -v valkey-server)
REDIS_CLI=$(command -v redis-cli || command -v valkey-cli)

SUITE_remote_redis_unix_PROBE() {
    if [ -z "${REDIS_SERVER}" ]; then
        echo "neither redis-server nor valkey-server found"
        return
    fi
    if ${REDIS_SERVER} --unixsocket /foo/redis.sock 2>&1 | grep -q "FATAL CONFIG FILE ERROR"; then
        # "Bad directive or wrong number of arguments"
        echo "redis-server without unixsocket"
        return
    fi
    if [ -z "${REDIS_CLI}" ]; then
        echo "neither redis-cli nor valkey-cli found"
        return
    fi
    if ! ${REDIS_CLI} -s /foo/redis.sock --version &> /dev/null; then
        # "Unrecognized option or bad number of args"
        echo "${REDIS_CLI} without socket option"
        return
    fi
}

start_redis_unix_server() {
    local socket="$1"
    local password="${2:-}"

    ${REDIS_SERVER} --bind localhost --unixsocket "${socket}" --port 0 >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! ${REDIS_CLI} -s "${socket}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ -n "${password}" ]; then
        ${REDIS_CLI} -s "${socket}" config set requirepass "${password}" &>/dev/null
    fi
}

SUITE_remote_redis_unix_SETUP() {
    unset CCACHE_NODIRECT

    export CRSH_LOGFILE="ccache-storage-redis_unix.log"

    generate_code 1 test.c
}

expect_number_of_redis_unix_cache_entries() {
    local expected=$1
    local socket=$2
    local actual

    actual=$(${REDIS_CLI} -s "$socket" keys "ccache:*" 2>/dev/null | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $socket"
    fi
}

SUITE_remote_redis_unix() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    socket=$(mktemp "${TMPDIR:-/tmp}/tmp.XXXXXX")
    redis_url="redis+unix:${socket}"
    export CCACHE_REMOTE_STORAGE="${redis_url}"

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
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Password"

    socket=$(mktemp "${TMPDIR:-/tmp}/tmp.XXXXXX")
    password=secret123
    redis_url="redis+unix://${password}@localhost${socket}"
    export CCACHE_REMOTE_STORAGE="${redis_url}"

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
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_redis_unix_cache_entries 2 "${socket}" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Unreachable server"

    export CCACHE_REMOTE_STORAGE="redis+unix:///foo"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_error 1
}
