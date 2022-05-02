SUITE_secondary_tkrzw_PROBE() {
    if ! $CCACHE --version | fgrep -q -- tkrzw-storage &> /dev/null; then
        echo "tkrzw-storage not available"
        return
    fi
    if ! command -v tkrzw_dbm_util &> /dev/null; then
        echo "tkrzw_dbm_util not found"
        return
    fi
}

SUITE_secondary_tkrzw_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_tkrzw_cache_entries() {
    local expected=$1
    local url=$2
    local actual

    dbm=$(echo $url | sed -e "s|^tkrzw://||")
    actual=$(tkrzw_dbm_util inspect "$dbm" | grep "Number of Records: " | sed -e "s/Number of Records: //")
    test -n "$actual" || test_failed_internal
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_secondary_tkrzw() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    dbm="$(mktemp).tkh"
    tkrzw_url="tkrzw://${dbm}"
    export CCACHE_SECONDARY_STORAGE="${tkrzw_url}"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_tkrzw_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_number_of_tkrzw_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_number_of_tkrzw_cache_entries 2 "$tkrzw_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_number_of_tkrzw_cache_entries 2 "$tkrzw_url" # result + manifest
}
