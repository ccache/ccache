SUITE_depend_different_headers_SETUP() {
    unset CCACHE_NODIRECT

    BASEDIR=$(readlink -n -f "$(dirname $0)")
    BASEDIR1="${BASEDIR}/test/dir1/"
    BASEDIR2="${BASEDIR}/test/dir2/"
    BASEDIR3="${BASEDIR}/test/dir3/"
     
    mkdir -p ${BASEDIR1} ${BASEDIR2} ${BASEDIR3}

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
    cp -f ${BASEDIR1}test.c ${BASEDIR2} 
    cp -f ${BASEDIR1}test.c ${BASEDIR3} 

    cat <<EOF >${BASEDIR1}/header.h
void test();
EOF

    cat <<EOF >${BASEDIR2}/header.h
#define CHANGE_THAT_AFFECTS_OBJECT_FILE
void test();
EOF

    cat <<EOF >${BASEDIR3}/header.h
#define CHANGE_THAT_DOES_NOT_AFFECT_OBJECT_FILE    
void test();
EOF

    backdate ${BASEDIR1}/header.h ${BASEDIR1}/test.c  ${BASEDIR2}/header.h ${BASEDIR2}/test.c ${BASEDIR3}/header.h ${BASEDIR3}/test.c  

    DEPSFLAGS_REAL="-MD -MF reference_test.d"
    DEPSFLAGS_CCACHE="-MD -MF test.d"
}

SUITE_depend_different_headers() {
    # This test case covers a case in the depend mode with unchanged source file between compilations, 
    # but with changed headers. 
    #
    # dir1 is the baseline 
    # dir2 has change in header which affects object file.
    # dir3 has change in header which does not affect object file.
    # -------------------------------------------------------------------------
    TEST "Base case"
    CLEAR_OBJS="rm -f test.o test.d"
    
    # compile dir1
    cd ${BASEDIR1}
    
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3      # .o + .manifest + .d

    #recompile dir1
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3

    #compile dir2 - this header changes object file compared to dir1
    cd ${BASEDIR2}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR2} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 5      # 2x .o, 2x .d, 1x manifest

    #compile dir3 - this header does not change object file compared to dir1
    cd ${BASEDIR3}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR3} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_object_files ${BASEDIR1}/test.o test.o # object file should be the same, but has different hash due to changed header
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3                    
    expect_stat 'files in cache' 7      # 3x .o, 3x .d, 1x manifest

    #recompile dir1
    cd ${BASEDIR1}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR1} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 7

    #recompile dir2
    cd ${BASEDIR2}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR2} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 7

    #recompile dir3
    cd ${BASEDIR3}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c
    ${CLEAR_OBJS}
    CCACHE_DEPEND=1 CCACHE_BASEDIR=${BASEDIR3} $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 4
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 7
}
