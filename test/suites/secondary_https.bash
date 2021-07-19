start_https_server() {
    local port="$1"
    local cache_dir="$2"
    local server_cert="$3"
    local server_key="server.key"

    mkdir -p "${cache_dir}"
    openssl req -subj "/CN=localhost/O=CCache/C=US" \
        -new -newkey rsa:2048 -sha256 -days 365 -nodes -x509 \
        -keyout "${server_key}" \
        -out "${server_cert}" &>openssl.log

    "${HTTP_SERVER}" --bind localhost --directory "${cache_dir}" "${port}" \
        --certfile "${server_cert}" --keyfile "${server_key}" \
        &>http-server.log &
    "${HTTP_CLIENT}" "https://localhost:${port}" &>http-client.log \
        || test_failed_internal "Cannot connect to server"
}

SUITE_secondary_https_PROBE() {
    if ! $CCACHE --version | fgrep -q -- https-storage &> /dev/null; then
        echo "https-storage not available"
        return
    fi
    if ! "${HTTP_SERVER}" --help >/dev/null 2>&1; then
        echo "cannot execute ${HTTP_SERVER} - Python 3 might be missing"
    fi
    if ! command -v openssl &> /dev/null; then
        echo "openssl not found"
        return
    fi
}

SUITE_secondary_https_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

SUITE_secondary_https() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    start_https_server 12781 secondary server.pem
    export CCACHE_SECONDARY_STORAGE="https://localhost:12781|cacert=server.pem"

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
}
