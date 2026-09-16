REDIS_SERVER=$(command -v redis-server || command -v valkey-server)
REDIS_CLI=$(command -v redis-cli || command -v valkey-cli)

SUITE_remote_oci_PROBE() {
    if ! $CCACHE --version | grep -Fq -- oci-storage &> /dev/null; then
        echo "oci-storage not available"
        return
    fi
    if [ -z "${OCI_TEST_REGISTRY:-}" ]; then
        echo "OCI_TEST_REGISTRY not set"
        return
    fi
    if [ -z "${REDIS_SERVER}" ] || [ -z "${REDIS_CLI}" ]; then
        echo "redis-server or redis-cli not found"
    fi
}

start_oci_test_redis() {
    local port=$1
    local i

    ${REDIS_SERVER} --bind localhost --port "${port}" --save '' --appendonly no >/dev/null &
    OCI_TEST_REDIS_PID=$!
    for ((i = 0; i < 100; i++)); do
        if ${REDIS_CLI} -p "${port}" ping >/dev/null 2>&1; then
            return
        fi
        sleep 0.1
    done
    test_failed_internal "Cannot start local Redis for OCI integration testing"
}

SUITE_remote_oci_SETUP() {
    unset CCACHE_NODIRECT
    generate_code 1 test.c
}

SUITE_remote_oci() {
    TEST "Store and retrieve"

    export CCACHE_REMOTE_STORAGE="oci://${OCI_TEST_REGISTRY}/ccache-ng/integration @insecure=true"

    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 2 # result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_hit 1

    # -------------------------------------------------------------------------
    TEST "Read-only OCI storage"

    generate_code 2 test.c
    export CCACHE_REMOTE_STORAGE="oci://${OCI_TEST_REGISTRY}/ccache-ng/integration @insecure=true read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 0

    # -------------------------------------------------------------------------
    TEST "Redis failure falls back to OCI and backfills"

    port=7788
    start_oci_test_redis "${port}"
    export CCACHE_REMOTE_STORAGE="redis://localhost:${port} helper=_builtin_ oci://${OCI_TEST_REGISTRY}/ccache-ng/integration @insecure=true"

    $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_write 2 # Redis result + manifest backfill

    $CCACHE -C >/dev/null
    kill "${OCI_TEST_REDIS_PID}"
    wait "${OCI_TEST_REDIS_PID}" 2>/dev/null || true

    $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_error 1
    expect_stat remote_storage_hit 2

    # -------------------------------------------------------------------------
    TEST "OCI storage through systemd credential directory"

    credential_dir="$PWD/oci-systemd-credentials"
    mkdir -p "${credential_dir}"
    chmod 700 "${credential_dir}"
    printf 'local-oci-test-credential\n' > "${credential_dir}/oci-token"
    chmod 600 "${credential_dir}/oci-token"
    generate_code 3 test.c

    CREDENTIALS_DIRECTORY="${credential_dir}" \
    CCACHE_REMOTE_STORAGE="oci://${OCI_TEST_REGISTRY}/ccache-ng/integration @insecure=true @systemd-credential=oci-token" \
      $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 2 # result + manifest

    $CCACHE -C >/dev/null
    CREDENTIALS_DIRECTORY="${credential_dir}" \
    CCACHE_REMOTE_STORAGE="oci://${OCI_TEST_REGISTRY}/ccache-ng/integration @insecure=true @systemd-credential=oci-token" \
      $CCACHE_COMPILE -c test.c
    expect_stat remote_storage_hit 3

    rm -f -- "${credential_dir}/oci-token"
    rmdir "${credential_dir}"
}
