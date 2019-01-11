SUITE_direct_gcc_PROBE() {
    if [[ $REAL_COMPILER != *"gcc"* ]]; then
        echo "Skipping GCC only test cases"
    fi
}

SUITE_direct_gcc_SETUP() {
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

    DEPENDENCIES_OUTPUT="expected_dependencies_output.d" $REAL_COMPILER -c test.c
    DEPENDENCIES_OUTPUT="expected_dependencies_output_target.d target.o" $REAL_COMPILER -c test.c
    SUNPRO_DEPENDENCIES="expected_sunpro_dependencies.d" $REAL_COMPILER -c test.c
    SUNPRO_DEPENDENCIES="expected_sunpro_dependencies_target.d target.o" $REAL_COMPILER -c test.c
    rm test.o
}

SUITE_direct_gcc() {
    # -------------------------------------------------------------------------
    TEST "DEPENDENCIES_OUTPUT environment variable"

    DEPENDENCIES_OUTPUT="other.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_dependencies_output.d

    DEPENDENCIES_OUTPUT="other.d" $REAL_COMPILER -c test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    DEPENDENCIES_OUTPUT="other.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_dependencies_output.d
    expect_equal_object_files reference_test.o test.o

    DEPENDENCIES_OUTPUT="different_name.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_dependencies_output.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "DEPENDENCIES_OUTPUT environment variable with target"

    DEPENDENCIES_OUTPUT="other.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_dependencies_output_target.d

    DEPENDENCIES_OUTPUT="other.d target.o" $REAL_COMPILER -c test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    DEPENDENCIES_OUTPUT="other.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_dependencies_output_target.d
    expect_equal_object_files reference_test.o test.o

    DEPENDENCIES_OUTPUT="different_name.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_dependencies_output_target.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "SUNPRO_DEPENDENCIES environment variable"

    SUNPRO_DEPENDENCIES="other.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_sunpro_dependencies.d

    SUNPRO_DEPENDENCIES="other.d" $REAL_COMPILER -c test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    SUNPRO_DEPENDENCIES="other.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_sunpro_dependencies.d
    expect_equal_object_files reference_test.o test.o

    SUNPRO_DEPENDENCIES="different_name.d" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_sunpro_dependencies.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "SUNPRO_DEPENDENCIES environment variable with target"

    SUNPRO_DEPENDENCIES="other.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_sunpro_dependencies_target.d

    SUNPRO_DEPENDENCIES="other.d target.o" $REAL_COMPILER -c test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    SUNPRO_DEPENDENCIES="other.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_sunpro_dependencies_target.d
    expect_equal_object_files reference_test.o test.o

    SUNPRO_DEPENDENCIES="different_name.d target.o" $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_sunpro_dependencies_target.d
    expect_equal_object_files reference_test.o test.o
}