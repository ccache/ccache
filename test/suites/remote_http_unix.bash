start_http_unix_server() {
    local socket="$1"
    local cache_dir="$2"
    local credentials="$3" # optional parameter

    mkdir -p "${cache_dir}"
    "${HTTP_SERVER}" --unix-socket "${socket}" --directory "${cache_dir}" \
        ${credentials:+--basic-auth ${credentials}} \
        &>http-server.log &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && [ ! -S "${socket}" ]; do
        sleep 0.1
        i=$((i + 1))
    done
    if [ ! -S "${socket}" ]; then
        test_failed_internal "HTTP server failed to start on Unix socket"
    fi
}

SUITE_remote_http_unix_PROBE() {
    if ! $CCACHE --version | grep -Fq -- http+unix-storage &> /dev/null; then
        echo "http+unix-storage not available"
        return
    fi
    if ! "${HTTP_SERVER}" --help 2>&1 | grep -q -- "--unix-socket"; then
        echo "http-server does not support --unix-socket"
        return
    fi
}

SUITE_remote_http_unix_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

SUITE_remote_http_unix() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    socket=$(mktemp)
    http_url="http+unix:${socket}"
    export CCACHE_REMOTE_STORAGE="${http_url}"

    start_http_unix_server "${socket}" remote

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 2 '*' remote # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 2 '*' remote # result + manifest

    # -------------------------------------------------------------------------
    TEST "With HTTP path"

    socket=$(mktemp)
    http_url="http+unix:${socket}?path=/cache"
    export CCACHE_REMOTE_STORAGE="${http_url}"

    mkdir -p remote/cache
    start_http_unix_server "${socket}" remote

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote/cache # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote/cache # result + manifest

    # -------------------------------------------------------------------------
    TEST "Basic auth"

    socket=$(mktemp)
    http_url="http+unix://somebody:secret123@localhost${socket}"
    export CCACHE_REMOTE_STORAGE="${http_url}"

    start_http_unix_server "${socket}" remote "somebody:secret123"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote # result + manifest
    expect_not_contains test.o.*.ccache-log secret123

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote # result + manifest

    # -------------------------------------------------------------------------
    TEST "Flat layout"

    socket=$(mktemp)
    http_url="http+unix:${socket}|layout=flat"
    export CCACHE_REMOTE_STORAGE="${http_url}"

    start_http_unix_server "${socket}" remote

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 2 '*' remote # result + manifest
    subdirs=$(find remote -type d | wc -l)
    if [ "${subdirs}" -ne 1 ]; then # "remote" itself counts as one
        test_failed "Expected no subdirectories in remote"
    fi

    # -------------------------------------------------------------------------
    TEST "Unreachable server"

    export CCACHE_REMOTE_STORAGE="http+unix:/nonexistent/socket"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_error 1
}
