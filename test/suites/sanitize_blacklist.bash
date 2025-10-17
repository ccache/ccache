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

    mkdir -p dir1/ dir2/
    cp blacklist.txt dir1/blacklist.txt
    cp blacklist.txt dir2/blacklist.txt

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
    TEST "base_dir OK"

    basedir1="$(pwd)/dir1"
    basedir2="$(pwd)/dir2"

    basedir=$basedir1
    cd $basedir

    $COMPILER -c -fsanitize-blacklist=blacklist.txt ../test1.c

    CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-blacklist=$basedir/blacklist.txt ../test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-blacklist=$basedir/blacklist.txt ../test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    basedir=$basedir2
    cd $basedir

    CCACHE_DEBUG=1 CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-blacklist=$basedir/blacklist.txt ../test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

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
