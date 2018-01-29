SUITE_serialize_diagnostics_PROBE() {
    touch test.c
    if ! $REAL_COMPILER -c --serialize-diagnostics \
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

    $REAL_COMPILER -c --serialize-diagnostics expected.dia test1.c

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_files expected.dia test.dia

    rm test.dia

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_files expected.dia test.dia

    # -------------------------------------------------------------------------
    TEST "Compile failed"

    echo "bad source" >error.c
    if $REAL_COMPILER -c --serialize-diagnostics expected.dia error.c 2>expected.stderr; then
        test_failed "Expected an error compiling error.c"
    fi

    $CCACHE_COMPILE -c --serialize-diagnostics test.dia error.c 2>test.stderr
    expect_stat 'compile failed' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 0
    expect_equal_files expected.dia test.dia
    expect_equal_files expected.stderr test.stderr

    # -------------------------------------------------------------------------
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
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 4

    cd ../dir2
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -w -MD -MF `pwd`/test.d -I`pwd`/include --serialize-diagnostics `pwd`/test.dia -c src/test.c -o `pwd`/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 4
}
