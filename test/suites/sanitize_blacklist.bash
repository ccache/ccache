SUITE_sanitize_blacklist_PROBE() {
    touch test.c blacklist.txt
    if ! $COMPILER -c -fsanitize-blacklist=blacklist.txt \
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

    $COMPILER -c -fsanitize-blacklist=blacklist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    echo "fun:bar" >blacklist.txt

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    # -------------------------------------------------------------------------
    TEST "Unsuccessful compilation"

    if $COMPILER -c -fsanitize-blacklist=nosuchfile.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    rm blacklist.txt

    if $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    expect_stat error_hashing_extra_file 1

    # -------------------------------------------------------------------------
    TEST "Multiple -fsanitize-blacklist"

    $COMPILER -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    echo "fun_2:foo" >blacklist2.txt

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    $CCACHE_COMPILE -c -fsanitize-blacklist=blacklist2.txt -fsanitize-blacklist=blacklist.txt test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
}
