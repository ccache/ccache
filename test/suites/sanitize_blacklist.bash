SUITE_sanitize_blacklist_PROBE() {
    touch test.c blacklist.txt
    if ! $REAL_COMPILER -c -fsanitize-blacklist=blacklist.txt \
         test.c 2>/dev/null; then
        echo "-fsanitize-blacklist not supported by compiler"
    fi
}

SUITE_sanitize_blacklist_SETUP() {
    generate_code 2 test1.c
    echo "fun:foo" >blacklist.txt
    echo "fun_1:foo" >blacklist2.txt

    unset CCACHE_NODIRECT
}

SUITE_sanitize_blacklist() {
    # -------------------------------------------------------------------------
    TEST "Compile OK"

    $REAL_COMPILER -c -fsanitize-blacklist=blacklist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    echo "fun:bar" >blacklist.txt

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 4

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 4

    # -------------------------------------------------------------------------
    TEST "Compile failed"

    if $REAL_COMPILER -c -fsanitize-blacklist=nosuchfile.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    rm blacklist.txt

    if $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    expect_stat 'error hashing extra file' 1

    # -------------------------------------------------------------------------
    TEST "Multiple -fsanitize-blacklist"

    $REAL_COMPILER -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    echo "fun_2:foo" >blacklist2.txt

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 4

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 4
}
