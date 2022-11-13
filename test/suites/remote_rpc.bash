SUITE_remote_rpc_PROBE() {
    if ! $CCACHE --version | fgrep -q -- rpc-storage &> /dev/null; then
        echo "rpc-storage not available"
        return
    fi
    rpc_server=$(dirname "$CCACHE")/src/storage/remote/rpc-server
    if [ ! -x "${rpc_server}" ] &> /dev/null; then
        echo "rpc-server not found"
        return
    fi
}

start_rpc_server() {
    local port="$1"
    local password="${2:-}"

    rpc_server=$(dirname "$CCACHE")/src/storage/remote/rpc-server
    rpc_storage=$(mktemp -d)
    if [ -n "${password}" ]; then
	rpc_passwd=$(mktemp)
	echo -n "${password}" >"${rpc_passwd}"
    fi
    CCACHE_REMOTE_ONLY=true CCACHE_REMOTE_STORAGE=file://${rpc_storage} \
    CCACHE_LOGFILE=$ABS_TESTDIR/server.log \
    ${rpc_server} --bind 127.0.0.1 --port "${port}" \
    ${rpc_passwd:+--auth --passwd ${rpc_passwd}} &
}

SUITE_remote_rpc_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_rpc_cache_entries() {
    local expected=$1
    #local url=$2

    expected=$((expected + 1))  # CACHEDIR.TAG
    expect_file_count ${expected} '*' "${rpc_storage}"
}

SUITE_remote_rpc() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    port=8888
    rpc_url="rpc://localhost:${port}"
    export CCACHE_REMOTE_STORAGE="${rpc_url}"

    start_rpc_server "${port}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    rm -r "${rpc_storage}"
    # -------------------------------------------------------------------------
    TEST "Password"

    port=8888
    password=secret123
    rpc_url="rpc://${password}@localhost:${port}"
    export CCACHE_REMOTE_STORAGE="${rpc_url}"

    start_rpc_server "${port}" "${password}"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest
    expect_not_contains test.o.*.ccache-log "${password}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_number_of_rpc_cache_entries 2 "$rpc_url" # result + manifest

    rm -r "${rpc_storage}"
    rm "${rpc_passwd}"
    # -------------------------------------------------------------------------
    TEST "Unreachable server"

    export CCACHE_REMOTE_STORAGE="rpc://localhost:1"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_error 1
}
