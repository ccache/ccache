start_http_server() {
    local port="$1"
    local cache_dir="$2"
    local credentials="$3" # optional parameter

    mkdir -p "${cache_dir}"
    "${HTTP_SERVER}" --bind localhost --directory "${cache_dir}" "${port}" \
        ${credentials:+--basic-auth ${credentials}} \
        &>http-server.log &
    "${HTTP_CLIENT}" "http://localhost:${port}" &>http-client.log \
        ${credentials:+--basic-auth ${credentials}} \
        || test_failed_internal "Cannot connect to server"
}

maybe_start_ipv6_http_server() {
    local port="$1"
    local cache_dir="$2"
    local credentials="$3" # optional parameter

    mkdir -p "${cache_dir}"
    "${HTTP_SERVER}" --bind "::1" --directory "${cache_dir}" "${port}" \
        ${credentials:+--basic-auth ${credentials}} \
        &>http-server.log &
    "${HTTP_CLIENT}" "http://[::1]:${port}" &>http-client.log \
        ${credentials:+--basic-auth ${credentials}} \
        || return 1
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
    expect_stat 'files in cache' 2 # fetched from secondary
    expect_file_count 2 '*' secondary # result + manifest

    # -------------------------------------------------------------------------
    TEST "Basic auth"

    start_http_server 12780 secondary "somebody:secret123"
    export CCACHE_SECONDARY_STORAGE="http://somebody:secret123@localhost:12780"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' secondary # result + manifest
    expect_not_contains test.o.ccache-log secret123

    # -------------------------------------------------------------------------
    TEST "Basic auth required"

    start_http_server 12780 secondary "somebody:secret123"
    # no authentication configured on client
    export CCACHE_SECONDARY_STORAGE="http://localhost:12780"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 0 '*' secondary # result + manifest
    expect_contains test.o.ccache-log "status code: 401"

    # -------------------------------------------------------------------------
    TEST "Basic auth failed"

    start_http_server 12780 secondary "somebody:secret123"
    export CCACHE_SECONDARY_STORAGE="http://somebody:wrong@localhost:12780"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 0 '*' secondary # result + manifest
    expect_not_contains test.o.ccache-log secret123
    expect_contains test.o.ccache-log "status code: 401"

     # -------------------------------------------------------------------------
    TEST "IPv6 address"

    if maybe_start_ipv6_http_server 12780 secondary; then
        export CCACHE_SECONDARY_STORAGE="http://[::1]:12780"

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
        expect_stat 'files in cache' 2 # fetched from secondary
        expect_file_count 2 '*' secondary # result + manifest
    fi

    # -------------------------------------------------------------------------
    TEST "Bazel layout"

    start_http_server 12780 secondary
    mkdir secondary/ac
    export CCACHE_SECONDARY_STORAGE="http://localhost:12780|layout=bazel"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    find secondary -ls
    expect_file_count 2 '*' secondary/ac # result + manifest
    if [ "$(ls secondary/ac | grep -Ec '^[0-9a-f]{64}$')" -ne 2 ]; then
        test_failed "Bazel layout filenames not as expected"
    fi

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' secondary/ac # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' secondary/ac # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # fetched from secondary
    expect_file_count 2 '*' secondary/ac # result + manifest

}
