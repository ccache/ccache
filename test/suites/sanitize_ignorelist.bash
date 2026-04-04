SUITE_sanitize_ignorelist_PROBE() {
    touch test.c ignorelist.txt
    if ! $COMPILER -c -fsanitize-ignorelist=ignorelist.txt \
         test.c 2>/dev/null; then
        echo "-fsanitize-ignorelist not supported by compiler"
    fi
}

SUITE_sanitize_ignorelist_SETUP() {
    generate_code 2 test1.c
    echo "fun:foo" >ignorelist.txt
    echo "fun_1:foo" >ignorelist2.txt

    mkdir -p dir1/ dir2/
    cp ignorelist.txt dir1/ignorelist.txt
    cp ignorelist.txt dir2/ignorelist.txt

    unset CCACHE_NODIRECT
}

SUITE_sanitize_ignorelist() {
    # -------------------------------------------------------------------------
    TEST "Compile OK, -fsanitize-ignorelist"

    $COMPILER -c -fsanitize-ignorelist=ignorelist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    echo "fun:bar" >ignorelist.txt

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    # -------------------------------------------------------------------------
    TEST "Compile OK, -fsanitize-blacklist"

    $COMPILER -c -fsanitize-blacklist=ignorelist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-blacklist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -fsanitize-blacklist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    echo "fun:bar" >ignorelist.txt

    $CCACHE_COMPILE -c -fsanitize-blacklist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    $CCACHE_COMPILE -c -fsanitize-blacklist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    # -------------------------------------------------------------------------
    TEST "base_dir OK"

    basedir1="$(pwd)/dir1"
    basedir2="$(pwd)/dir2"

    basedir=$basedir1
    cd $basedir

    $COMPILER -c -fsanitize-ignorelist=ignorelist.txt ../test1.c

    CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-ignorelist=$basedir/ignorelist.txt ../test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-ignorelist=$basedir/ignorelist.txt ../test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    basedir=$basedir2
    cd $basedir

    CCACHE_DEBUG=1 CCACHE_BASEDIR="${basedir}" $CCACHE_COMPILE -c -fsanitize-ignorelist=$basedir/ignorelist.txt ../test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # -------------------------------------------------------------------------
    TEST "Unsuccessful compilation"

    if $COMPILER -c -fsanitize-ignorelist=nosuchfile.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    rm ignorelist.txt

    if $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist.txt test1.c 2>expected.stderr; then
        test_failed "Expected an error compiling test1.c"
    fi

    expect_stat error_hashing_extra_file 1

    # -------------------------------------------------------------------------
    TEST "Multiple -fsanitize-ignorelist"

    $COMPILER -c -fsanitize-ignorelist=ignorelist2.txt -fsanitize-ignorelist=ignorelist.txt test1.c

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist2.txt -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist2.txt -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    echo "fun_2:foo" >ignorelist2.txt

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist2.txt -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    $CCACHE_COMPILE -c -fsanitize-ignorelist=ignorelist2.txt -fsanitize-ignorelist=ignorelist.txt test1.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
}
