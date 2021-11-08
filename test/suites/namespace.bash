SUITE_namespace_SETUP() {
    unset CCACHE_NODIRECT
    echo 'int x;' >test1.c
    echo 'int y;' >test2.c
}

SUITE_namespace() {
    # -------------------------------------------------------------------------
    TEST "Namespace makes entries isolated"

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    CCACHE_NAMESPACE=a $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NAMESPACE=a $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2

    CCACHE_NAMESPACE=b $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 3

    CCACHE_NAMESPACE=b $CCACHE_COMPILE -c test1.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "--evict-namespace + --evict-older-than"

    CCACHE_NAMESPACE="a" $CCACHE_COMPILE -c test1.c
    result_file="$(find $CCACHE_DIR -name '*R')"
    backdate "$result_file"
    for ns in a b c; do
        CCACHE_NAMESPACE="$ns" $CCACHE_COMPILE -c test2.c
    done
    expect_stat cache_miss 4
    expect_stat files_in_cache 8

    $CCACHE --evict-namespace d >/dev/null
    expect_stat files_in_cache 8

    $CCACHE --evict-namespace c >/dev/null
    expect_stat files_in_cache 6

    $CCACHE --evict-namespace a --evict-older-than 1d >/dev/null
    expect_stat files_in_cache 5

    $CCACHE --evict-namespace a >/dev/null
    expect_stat files_in_cache 2
}
