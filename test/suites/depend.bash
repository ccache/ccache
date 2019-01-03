SUITE_depend_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >test.c
// test.c
#include "test1.h"
#include "test2.h"
EOF
    cat <<EOF >test1.h
#include "test3.h"
int test1;
EOF
    cat <<EOF >test2.h
int test2;
EOF
    cat <<EOF >test3.h
int test3;
EOF
    backdate test1.h test2.h test3.h

    $REAL_COMPILER -c -Wp,-MD,expected.d test.c
    $REAL_COMPILER -c -Wp,-MMD,expected_mmd.d test.c
    rm test.o

    DEPSFLAGS_REAL="-MP -MMD -MF reference_test.d"
    DEPSFLAGS_CCACHE="-MP -MMD -MF test.d"
}

SUITE_depend() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c

    CCACHE_DEPEND=1 $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3 # .o + .manifest + .d


    CCACHE_DEPEND=1 $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3

    # -------------------------------------------------------------------------
    TEST "No explicit dependency file"

    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3 # .o + .manifest + .d

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3

    # -------------------------------------------------------------------------
    TEST "stderr from both preprocessor and compiler"

    cat <<EOF >cpp-warning.c
#if FOO
// Trigger preprocessor warning about extra token after #endif.
#endif FOO
int stderr(void)
{
  // Trigger compiler warning by having no return statement.
}
EOF
    $COMPILER -MD -Wall -W -c cpp-warning.c 2>stderr-baseline.txt

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -Wall -W -c cpp-warning.c 2>stderr-orig.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content stderr-orig.txt "`cat stderr-baseline.txt`"

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content stderr-mf.txt "`cat stderr-baseline.txt`"

    # FIXME: add more test cases (see direct.bash for inspiration)
}
