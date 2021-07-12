SUITE_secondary_kyoto_PROBE() {
    if ! command -v ktserver &> /dev/null; then
        echo "ktserver not found"
        return
    fi
    if ! command -v ktremotemgr &> /dev/null; then
        echo "ktremotemgr not found"
        return
    fi
}

SUITE_secondary_kyoto_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_SECONDARY_STORAGE="kt://localhost:2012"

    generate_code 1 test.c
}

expect_number_of_cache_entries_kyoto() {
    local expected=$1
    local host="localhost"
    local port=$2
    local actual

    actual=$(ktremotemgr list -host "$host" -port "$port" | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $host:$port"
    fi
}

SUITE_secondary_kyoto() {
    if $HOST_OS_APPLE; then
         # no coreutils on darwin by default, perl rather than gtimeout
         function timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
    fi
    timeout 10 ktserver -host localhost -port 2012 -ls &
    sleep 0.1 # wait for boot
    ktremotemgr version -port 2012

    function expect_number_of_cache_entries() {
      expect_number_of_cache_entries_kyoto "$@"
    }
    secondary=2012

    # -------------------------------------------------------------------------
    TEST "Base case"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    ktremotemgr clear -port 2012

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_cache_entries 2 "$secondary" # result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_number_of_cache_entries 2 "$secondary" # result + manifest
}
