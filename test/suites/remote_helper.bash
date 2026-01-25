start_test_helper() {
    local endpoint="$1"

    export CRSH_IPC_ENDPOINT="${endpoint}"
    export CRSH_URL="dummy"

    "${STORAGE_TEST_HELPER}" &

    local attempts=0
    while [ $attempts -lt 50 ]; do
        if "${STORAGE_TEST_CLIENT}" "${endpoint}" ping >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
        attempts=$((attempts + 1))
    done

    test_failed_internal "Test storage helper failed to start (no response to ping)"
}

SUITE_remote_helper_PROBE() {
    if ! python3 --version >/dev/null 2>&1; then
        echo "python3 is not available"
    fi
}

SUITE_remote_helper_SETUP() {
    unset CCACHE_NODIRECT

    export CRSH_IDLE_TIMEOUT="10"
    export CRSH_LOGFILE="ccache-storage-test.log"
    generate_code 1 test.c
}

SUITE_remote_helper() {
    # -------------------------------------------------------------------------
    TEST "Helper auto-spawn and basic operations"

    export CCACHE_REMOTE_STORAGE="test://dummy helper=${STORAGE_TEST_HELPER}"

    # First compilation: miss, ccache spawns helper and stores
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2 # result + manifest
    expect_stat remote_storage_write 2 # result + manifest

    # Second compilation: local hit (helper stays alive)
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1

    # Clear local cache
    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    # Third compilation: remote hit from spawned helper
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from helper
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # result + manifest
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2

    $CCACHE --stop-storage-helpers

    # -------------------------------------------------------------------------
    TEST "Helper reuse across compilations"

    export CCACHE_REMOTE_STORAGE="test://dummy2 helper=${STORAGE_TEST_HELPER}"

    # Multiple compilations should reuse same spawned helper
    for i in 1 2 3; do
        generate_code $i test.c
        $CCACHE_COMPILE -c test.c
        expect_stat cache_miss $i
    done

    expect_stat files_in_cache 6 # 3 results + 3 manifests

    $CCACHE --stop-storage-helpers

    # -------------------------------------------------------------------------
    TEST "Direct crsh: connection"

    local endpoint="$PWD/test.sock"

    if $HOST_OS_WINDOWS; then
        endpoint="ccache-test-$$"
    fi

    start_test_helper "${endpoint}"

    export CCACHE_REMOTE_STORAGE="crsh:${endpoint}"

    # First compilation - miss, store to test helper
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2 # result + manifest
    expect_stat remote_storage_write 2 # result + manifest

    # Second compilation - local hit
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1

    # Clear local cache
    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    # Third compilation - remote hit from test helper
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from test helper
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # result + manifest
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2

    # -------------------------------------------------------------------------
    TEST "Connection failure handling"

    local endpoint="$PWD/nonexistent.sock"

    if $HOST_OS_WINDOWS; then
        endpoint="ccache-nonexistent-$$"
    fi

    # Don't start helper - test connection failure
    export CCACHE_REMOTE_STORAGE="crsh:${endpoint}"

    # Should fall back to local-only operation
    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat remote_storage_error 1
}
