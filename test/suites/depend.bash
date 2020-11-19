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

set_up_different_sets_of_headers_test() {
    BASEDIR=$(pwd)
    BASEDIR1="$BASEDIR/test/dir1"
    BASEDIR2="$BASEDIR/test/dir2"
    BASEDIR3="$BASEDIR/test/dir3"
    BASEDIR4="$BASEDIR/test/dir4"

    mkdir -p $BASEDIR1 $BASEDIR2 $BASEDIR3 $BASEDIR4

    cat <<EOF >$BASEDIR1/test.c
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
    cp -f "$BASEDIR1/test.c" $BASEDIR2
    cp -f "$BASEDIR1/test.c" $BASEDIR3
    cp -f "$BASEDIR1/test.c" $BASEDIR4

    cat <<EOF >"$BASEDIR1/header.h"
void test();
EOF

    cat <<EOF >"$BASEDIR2/header.h"
#define CHANGE_THAT_AFFECTS_OBJECT_FILE
void test();
EOF

    cat <<EOF >"$BASEDIR3/header.h"
#define CHANGE_THAT_DOES_NOT_AFFECT_OBJECT_FILE
void test();
EOF

    cat <<EOF >"$BASEDIR4/header.h"
#include "header2.h"
void test();
EOF
    cat <<EOF >"$BASEDIR4/header2.h"
static void some_function(){};
EOF

    backdate "$BASEDIR1/header.h" "$BASEDIR1/test.c"
    backdate "$BASEDIR2/header.h" "$BASEDIR2/test.c"
    backdate "$BASEDIR3/header.h" "$BASEDIR3/test.c"
    backdate "$BASEDIR4/header.h" "$BASEDIR4/test.c" "$BASEDIR4/header2.h"

    DEPFLAGS="-MD -MF test.d"
}

generate_reference_compiler_output() {
    rm -f *.o *.d
    $REAL_COMPILER $DEPFLAGS -c -o test.o test.c
    mv test.o reference_test.o
    mv test.d reference_test.d
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
    expect_stat 'files in cache' 2 # result + manifest

    CCACHE_DEPEND=1 $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "No dependency file"

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MP -MMD -MF /dev/null -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # result + manifest

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MP -MMD -MF /dev/null -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "No explicit dependency file"

    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # result + manifest

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "Dependency file paths converted to relative if CCACHE_BASEDIR specified"

    CCACHE_DEPEND=1 CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c "`pwd`/test.c"
    if grep -q "[^.]/test.c" "test.d"; then
        test_failed "Dependency file does not contain relative path to test.c"
    fi

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
    $REAL_COMPILER -MD -Wall -W -c cpp-warning.c 2>stderr-baseline.txt

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -Wall -W -c cpp-warning.c 2>stderr-orig.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_content stderr-orig.txt "`cat stderr-baseline.txt`"

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MD -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_content stderr-mf.txt "`cat stderr-baseline.txt`"

    # -------------------------------------------------------------------------
    # This test case covers a case in depend mode with unchanged source file
    # between compilations, but with changed headers. Header contents do not
    # affect the common hash (by which the manifest is stored in the cache),
    # only the object's hash.
    #
    # dir1 is baseline
    # dir2 has a change in header which affects object file
    # dir3 has a change in header which does not affect object file
    # dir4 has an additional include header which should change the dependency file
    TEST "Different sets of headers for the same source code"

    set_up_different_sets_of_headers_test

    # Compile dir1.
    cd $BASEDIR1

    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2      # result + manifest

    # Recompile dir1 first time.
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    # Compile dir2. dir2 header changes the object file compared to dir1.
    cd $BASEDIR2
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 3      # 2x result, 1x manifest

    # Compile dir3. dir3 header change does not change object file compared to
    # dir1, but ccache still adds an additional .o/.d file in the cache due to
    # different contents of the header file.
    cd $BASEDIR3
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 4      # 3x result, 1x manifest

    # Compile dir4. dir4 header adds a new dependency.
    cd $BASEDIR4
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 5      # 4x result, 1x manifest

    # Recompile dir1 second time.
    cd $BASEDIR1
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 5

    # Recompile dir2.
    cd $BASEDIR2
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 5

    # Recompile dir3.
    cd $BASEDIR3
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_stat 'cache hit (direct)' 4
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 5

    # Recompile dir4.
    cd $BASEDIR4
    generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_COMPILE $DEPFLAGS -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat 'cache hit (direct)' 5
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 4
    expect_stat 'files in cache' 5

    # -------------------------------------------------------------------------
    TEST "Source file with special characters"

    touch 'file with$special#characters.c'
    $REAL_COMPILER -MMD -c 'file with$special#characters.c'
    mv 'file with$special#characters.d' reference.d

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MMD -c 'file with$special#characters.c'
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_content 'file with$special#characters.d' reference.d

    rm 'file with$special#characters.d'

    CCACHE_DEPEND=1 $CCACHE_COMPILE -MMD -c 'file with$special#characters.c'
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_content 'file with$special#characters.d' reference.d
}
