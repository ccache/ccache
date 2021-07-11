start_http_server() {
    local port="$1"
    local cache_dir="$2"

    mkdir -p "${cache_dir}"
    "${HTTP_SERVER}" --bind localhost --directory "${cache_dir}" "${port}" \
        &>http-server.log &
    "${HTTP_CLIENT}" "http://localhost:${port}" &>http-client.log \
        || test_failed_internal "Cannot connect to server"
}

SUITE_secondary_http_PROBE() {
    if ! "${HTTP_SERVER}" --help >/dev/null 2>&1; then
        echo "cannot execute ${HTTP_SERVER} - Python 3 might be missing"
    fi
}

SUITE_secondary_http_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

SUITE_secondary_http() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    start_http_server 12780 secondary
    export CCACHE_SECONDARY_STORAGE="http://localhost:12780"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' secondary # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' secondary # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' secondary # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' secondary # result + manifest
}
