SUITE_secondary_tkrzw_rpc_unix_PROBE() {
    if ! $CCACHE --version | fgrep -q -- tkrzw-storage &> /dev/null; then
        echo "tkrzw-storage not available"
        return
    fi
    if ! command -v tkrzw_server &> /dev/null; then
        echo "tkrzw_server not found"
        return
    fi
    if ! command -v tkrzw_dbm_remote_util &> /dev/null; then
        echo "tkrzw_dbm_remote_util not found"
        return
    fi
}

start_tkrzw_unix_server() {
    local socket="$1"

    dbm="$(mktemp).tkh"
    tkrzw_server --address "unix:${socket}" "$dbm" >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! tkrzw_dbm_remote_util echo --address "unix:${socket}" &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done
}

SUITE_secondary_tkrzw_rpc_unix_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_tkrzw_rpc_unix_cache_entries() {
    local expected=$1
    local url=$2
    local actual

    address=$(echo $url | sed -e "s|^tkrzw+unix://|unix:|")
    actual=$(tkrzw_dbm_remote_util inspect --address "$address" | grep "num_records=" | sed -e "s/num_records=//")
    test -n "$actual" || test_failed_internal
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_secondary_tkrzw_rpc_unix() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    socket=$(mktemp).sock
    tkrzw_url="tkrzw+unix://${socket}"
    export CCACHE_SECONDARY_STORAGE="${tkrzw_url}"

    start_tkrzw_unix_server "${socket}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_tkrzw_rpc_unix_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_tkrzw_rpc_unix_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_tkrzw_rpc_unix_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_number_of_tkrzw_rpc_unix_cache_entries 2 "$tkrzw_url" # result + manifest
}
