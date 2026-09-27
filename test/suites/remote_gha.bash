GHA_CACHE_SERVER="${ABS_ROOT_DIR}/gha-cache-server"

SUITE_remote_gha_PROBE() {
    if ! $CCACHE --version | grep -Fq -- gha-storage &> /dev/null; then
        echo "gha-storage not available"
        return
    fi
    if ! python3 --version >/dev/null 2>&1; then
        echo "python3 not found"
        return
    fi
    if ! probe_tcp_server_socket; then
        echo "creating a local TCP server socket is not permitted"
    fi
}

start_gha_cache_server() {
    local port=$1
    local i

    python3 "${GHA_CACHE_SERVER}" "${port}" >gha-cache-server.log 2>&1 &
    for ((i = 0; i < 100; i++)); do
        if python3 -c "import socket; socket.create_connection(('localhost', ${port}), 1).close()" >/dev/null 2>&1; then
            return
        fi
        sleep 0.1
    done
    test_failed_internal "Cannot start local GitHub Actions cache test server"
}

SUITE_remote_gha_SETUP() {
    unset CCACHE_NODIRECT
    export ACTIONS_RUNTIME_TOKEN=integration-token
    generate_code 1 test.c
}

SUITE_remote_gha() {
    TEST "Store and retrieve v2"

    port=12782
    start_gha_cache_server "${port}"
    export CCACHE_REMOTE_STORAGE="gha://integration @url=http://localhost:${port}/runtime/ @service-version=v2"

    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 2 # result + manifest

    $CCACHE -C >/dev/null
    $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_hit 1

    # -------------------------------------------------------------------------
    TEST "Rate-limited write"

    port=12782
    start_gha_cache_server "${port}"
    export CCACHE_REMOTE_STORAGE="gha://rate-limited @url=http://localhost:${port}/runtime/ @service-version=v2"

    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 0
    generate_code 2 test.c
    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 2
    expect_stat remote_storage_write 0
    python3 -c "import urllib.request; print(urllib.request.urlopen('http://localhost:${port}/stats').read().decode())" >gha-stats.log
    expect_contains gha-stats.log '"rate_limited_reserves": 1'

    # -------------------------------------------------------------------------
    TEST "Store and retrieve v1"

    generate_code 3 test.c
    export CCACHE_REMOTE_STORAGE="gha://integration-v1 @url=http://localhost:${port}/runtime/ @service-version=v1"
    start_gha_cache_server "${port}"

    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1

    $CCACHE -C >/dev/null
    $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_hit 1
}
