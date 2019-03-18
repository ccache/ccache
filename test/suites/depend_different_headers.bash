SUITE_depend_different_headers_SETUP() {
    unset CCACHE_NODIRECT

    BASEDIR=`pwd`
    BASEDIR1="${BASEDIR}/test/dir1/"
    BASEDIR2="${BASEDIR}/test/dir2/"
    BASEDIR3="${BASEDIR}/test/dir3/"
    BASEDIR4="${BASEDIR}/test/dir4/"

    mkdir -p ${BASEDIR1} ${BASEDIR2} ${BASEDIR3} ${BASEDIR4}

    cat <<EOF >${BASEDIR1}/test.c
#include "header.h"
#include <stdio.h>

void test(){
#ifdef CHANGE_THAT_AFFECTS_OBJECT_FILE
printf("with change");
#else
printf("no change");
#endif
}
EOF
    cp -f "${BASEDIR1}/test.c" ${BASEDIR2}
    cp -f "${BASEDIR1}/test.c" ${BASEDIR3}
    cp -f "${BASEDIR1}/test.c" ${BASEDIR4}

    cat <<EOF >"${BASEDIR1}/header.h"
void test();
EOF

    cat <<EOF >"${BASEDIR2}/header.h"
#define CHANGE_THAT_AFFECTS_OBJECT_FILE
void test();
EOF

    cat <<EOF >"${BASEDIR3}/header.h"
#define CHANGE_THAT_DOES_NOT_AFFECT_OBJECT_FILE
void test();
EOF

    cat <<EOF >"${BASEDIR4}/header.h"
#include "header2.h"
void test();
EOF
    cat <<EOF >"${BASEDIR4}/header2.h"
static void some_function(){};
EOF

    backdate "${BASEDIR1}/header.h" "${BASEDIR1}/test.c"
    backdate "${BASEDIR2}/header.h" "${BASEDIR2}/test.c"
    backdate "${BASEDIR3}/header.h" "${BASEDIR3}/test.c"
    backdate "${BASEDIR4}/header.h" "${BASEDIR4}/test.c" "${BASEDIR4}/header2.h"

    DEPFLAGS="-MD -MF test.d"
}

generate_reference_compiler_output() {
    rm -f *.o *.d
    ${REAL_COMPILER} ${DEPFLAGS} -c -o test.o test.c
    mv test.o reference_test.o
    mv test.d reference_test.d
}

SUITE_depend_different_headers() {
    # This test case covers a case in depend mode with unchanged source file between compilations,
    # but with changed headers. Header contents do not affect the common hash (by which .manifest
    # is stored in cache), only the object's hash.
    #
    # dir1 is baseline
    # dir2 has a change in header which affects object file
    # dir3 has a change in header which does not affect object file
    # dir4 has an additional include header which should change the dependency file
    # -------------------------------------------------------------------------
    TEST "Depend mode - test unique sets of headers for the same source code"

    # Compile dir1
    cd ${BASEDIR1}

    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3      # .o + .manifest + .d

    # Recompile dir1 - 1st time
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3

    # Compile dir2
    # dir2 header changes the object file compared to dir1
    cd ${BASEDIR2}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR2} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 5      # 2x .o, 2x .d, 1x manifest

    # Compile dir3
    # dir3 header change does not change object file compared to dir1, but ccache still adds
    # additional .o/.d file in the cache due to different contents of the header file
    cd ${BASEDIR3}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR3} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 7      # 3x .o, 3x .d, 1x manifest

    # Compile dir4
    # dir4 header adds a new dependency
    cd ${BASEDIR4}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR4} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_different_files reference_test.d ${BASEDIR1}/test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 9      # 4x .o, 4x .d, 1x manifest

    # Recompile dir1 - 2nd time
    cd ${BASEDIR1}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 9

    # Recompile dir2
    cd ${BASEDIR2}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR2} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 9

    # Recompile dir3
    cd ${BASEDIR3}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR3} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_stat 'cache hit (direct)' 4
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 9

    # Recompile dir4
    cd ${BASEDIR4}
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR4} $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_files reference_test.d test.d
    expect_different_files reference_test.d ${BASEDIR1}/test.d
    expect_stat 'cache hit (direct)' 5
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 9
}
