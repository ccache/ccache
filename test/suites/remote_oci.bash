SUITE_remote_oci_PROBE() {
    if ! $CCACHE --version | grep -Fq -- oci-storage &> /dev/null; then
        echo "oci-storage not available"
        return
    fi
    if [ -z "${OCI_TEST_REGISTRY:-}" ]; then
        echo "OCI_TEST_REGISTRY not set"
    fi
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
}
