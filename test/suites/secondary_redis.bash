SUITE_secondary_redis_PROBE() {
     if [ "`which redis-server`" = "" ]; then
        echo "no redis-server"
        return
    fi
    if [ "`which redis-cli`" = "" ]; then
        echo "no redis-cli"
        return
    fi
}

SUITE_secondary_redis_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_SECONDARY_STORAGE="redis://localhost:7777"

    redis-server --bind localhost --port 7777 &
    sleep 1 # wait for boot
    redis-cli -p 7777 ping
    trap "kill $!" EXIT

    generate_code 1 test.c
}

SUITE_secondary_redis() {
    # -------------------------------------------------------------------------
    TEST "Redis"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2

    redis-cli -p 7777 keys "ccache:*"
}
