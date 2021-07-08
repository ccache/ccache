start_http_server() {
    local host="127.0.0.1"
    local port="8080"
    SECONDARY_HTTP_URL="http://${host}:${port}/"

    mkdir "secondary"
    "${HTTP_SERVER}" --bind "${host}" --directory "secondary" "${port}" >http-server.log 2>&1 &
    "${HTTP_CLIENT}" "${SECONDARY_HTTP_URL}" >http-client.log 2>&1 || test_failed_internal "Cannot connect to server"
}

SUITE_secondary_http_PROBE() {
    if ! "${HTTP_SERVER}" --help >/dev/null 2>&1; then
        echo "cannot execute ${HTTP_SERVER} - Python 3 might be missing"
    fi
}

SUITE_secondary_http_SETUP() {
    unset CCACHE_NODIRECT

    local subdir="${CURRENT_TEST// /_}"
    export CCACHE_SECONDARY_STORAGE="${SECONDARY_HTTP_URL}/${subdir}"
    SECONDARY_HTTP_DIR="${ABS_TESTDIR}/secondary/${subdir}"
    mkdir "${SECONDARY_HTTP_DIR}"

    generate_code 1 test.c
}

SUITE_secondary_http() {
    start_http_server

    # -------------------------------------------------------------------------
    TEST "Base case"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_file_count 2 '*' $SECONDARY_HTTP_DIR # result + manifest
}
