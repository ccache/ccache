SUITE_serialize_diagnostics_PROBE() {
    touch test.c
    if ! $COMPILER -c --serialize-diagnostics \
         test1.dia test.c 2>/dev/null; then
        echo "--serialize-diagnostics not supported by compiler"
    fi
}

SUITE_serialize_diagnostics_SETUP() {
    generate_code 1 test1.c
}

SUITE_serialize_diagnostics() {
    # -------------------------------------------------------------------------
    TEST "Compile OK"

    $COMPILER -c --serialize-diagnostics expected.dia test1.c

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content expected.dia test.dia

    rm test.dia

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content expected.dia test.dia

    # -------------------------------------------------------------------------
    TEST "Unsuccessful compilation"

    echo "bad source" >error.c
    if $COMPILER -c --serialize-diagnostics expected.dia error.c 2>expected.stderr; then
        test_failed "Expected an error compiling error.c"
    fi

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia error.c 2>test.stderr
    expect_stat compile_failed 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    expect_equal_content expected.dia test.dia
    expect_equal_text_content expected.stderr test.stderr

    # -------------------------------------------------------------------------
if $RUN_WIN_XFAIL; then
    TEST "--serialize-diagnostics + CCACHE_BASEDIR"

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2
    backdate dir1/include/test.h dir2/include/test.h

    cat <<EOF >stderr.h
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF

    unset CCACHE_NODIRECT

    cd dir1
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -w -MD -MF `pwd`/test.d -I`pwd`/include --serialize-diagnostics `pwd`/test.dia -c src/test.c -o `pwd`/test.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -w -MD -MF `pwd`/test.d -I`pwd`/include --serialize-diagnostics `pwd`/test.dia -c src/test.c -o `pwd`/test.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
fi
}
