ctc_PROBE() {
    if [ -z "$REAL_CTC" ]; then
        echo "ctc is not available"
    fi
}

ctc_SETUP() {
    TASKING_COMPILER=$REAL_CTC
    TASKING_COMPARE_OUTPUT=expect_ctc_src_equal
    TASKING_TARGET=""
    TASKING_FLAGS="--core=tc1.6.2 $TASKING_TARGET"
    TASKING_DEPFLAGS="--dep-file $TASKING_FLAGS"
    TASKING_EXTENSION="src"

    CCACHE_TASKING="$CCACHE $TASKING_COMPILER"
}

expect_ctc_src_equal() {
    if [ ! -e "$1" ]; then
        test_failed_internal "expect_ctc_src_equal: $1 missing"
    fi
    if [ ! -e "$2" ]; then
        test_failed_internal "expect_ctc_src_equal: $2 missing"
    fi
    # remove the compiler invocation lines that could differ
    cp $1 $1_for_check
    cp $2 $2_for_check
    sed_in_place '/.compiler_invocation/d' $1_for_check $2_for_check
    sed_in_place '/;source/d' $1_for_check $2_for_check

    if ! cmp -s "$1_for_check" "$2_for_check"; then
        test_failed_internal "$1 and $2 differ: $(echo; diff -u "$1_for_check" "$2_for_check")"
    fi
}

ctc_base_tests() {
    # -------------------------------------------------------------------------
    TEST "Preprocessor base case"

    generate_code 1 test1.c

    # test compilation without ccache to get a reference file
    $TASKING_COMPILER $TASKING_FLAGS -o reference_test1.${TASKING_EXTENSION} test1.c

    # First compile.
    $CCACHE_TASKING $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}

    # Second compile.
    $CCACHE_TASKING $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}

    # Third compile, test output option
    $CCACHE_TASKING $TASKING_FLAGS --output=test1.${TASKING_EXTENSION} test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}

    # Test for a different core -> parameter stays in the hash along with the preprocessor output
    $CCACHE_TASKING --core=tc1.3 $TASKING_TARGET test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 2

    # Test for a different core -> parameter stays in the hash along with the preprocessor output
    $CCACHE_TASKING --core=tc1.3 --align=4 $TASKING_TARGET test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 3
    expect_stat files_in_cache 3

    # test with the same option in a file
    echo "--core=tc1.3" > file.opt
    echo "--align=4" >> file.opt
    echo "$TASKING_TARGET " >> file.opt
    $CCACHE_TASKING -f file.opt test1.c
    expect_stat preprocessed_cache_hit 3
    expect_stat cache_miss 3
    expect_stat files_in_cache 3

    # test with the same option in a file
    $CCACHE_TASKING --option-file=file.opt test1.c
    expect_stat preprocessed_cache_hit 4
    expect_stat cache_miss 3
    expect_stat files_in_cache 3

    # modify the C file
    generate_code 2 test1.c
    $CCACHE_TASKING --option-file=file.opt test1.c
    expect_stat preprocessed_cache_hit 4
    expect_stat cache_miss 4
    expect_stat files_in_cache 4

    # -------------------------------------------------------------------------
    TEST "Direct mode base case"
    generate_code 1 test1.c

    unset CCACHE_NODIRECT

    # First compile.
    $CCACHE_TASKING $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Second compile.
    $CCACHE_TASKING $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # modify the C file
    generate_code 2 test1.c
    $CCACHE_TASKING $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    # -------------------------------------------------------------------------
    TEST "Direct mode header modification case"

    mkdir -p subdir/src subdir/include
    cat <<EOF >subdir/src/test.c
#include <test.h>
int foo(int x) { return test + x; }
EOF
    cat <<EOF >subdir/include/test.h
int test;
EOF

    # after creation of the file, wait a couple of seconds
    sleep 2

    unset CCACHE_NODIRECT

    # First compile.
    $CCACHE_TASKING -I subdir/include $TASKING_FLAGS subdir/src/test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Second compile -> direct hit
    $CCACHE_TASKING -I subdir/include $TASKING_FLAGS subdir/src/test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # modify the header file
    cat <<EOF >subdir/include/test.h
int test;
int test2;
EOF

    # after modification of the file, wait a couple of seconds
    sleep 2

    $CCACHE_TASKING -I subdir/include $TASKING_FLAGS subdir/src/test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    # here we have only three files because the same key is used to match the command line, but the manifest (include files sha)
    # changed
    expect_stat files_in_cache 3

    # -------------------------------------------------------------------------
    TEST "Extra long CLI parameters"

    mkdir -p subdir/src subdir/include
    cat <<EOF >subdir/src/test.c
#include <test.h>
int foo(int x) { return test + x; }
EOF
    cat <<EOF >subdir/include/test.h
int test;
EOF

    # after creation of the file, wait a couple of seconds
    sleep 2

    unset CCACHE_NODIRECT

    local i
    local params=""
    for ((i = 1; i <= 1000; i++)); do
        params="$params -I subdir/include"
    done

    # fallback to original compiler (check output to stdout)
    $CCACHE_TASKING $params -n $TASKING_FLAGS subdir/src/test.c
    expect_stat output_to_stdout 1

    remove_cache

    # with arguments
    $CCACHE_TASKING $params $TASKING_FLAGS subdir/src/test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    remove_cache

    # with option
    rm -f optionfile
    for ((i = 1; i <= 1000; i++)); do
         echo " -I subdir/include" >>optionfile
    done

    $CCACHE_TASKING -f optionfile $TASKING_FLAGS subdir/src/test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    return 0
}

ctc_basedir_setup() {
    unset CCACHE_NODIRECT

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
}

ctc_basedir_tests() {

    # -------------------------------------------------------------------------
    TEST "Enabled CCACHE_BASEDIR"
    ctc_basedir_setup

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_TASKING $TASKING_FLAGS -I`pwd`/include src/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    cd ../dir2
    CCACHE_BASEDIR="`pwd`" $CCACHE_TASKING $TASKING_FLAGS -I`pwd`/include src/test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Disabled (default) CCACHE_BASEDIR"
    ctc_basedir_setup

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_TASKING $TASKING_FLAGS -I`pwd`/include src/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # CCACHE_BASEDIR="" is the default:
    $CCACHE_TASKING $TASKING_FLAGS -I`pwd`/include src/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    return 0
}

ctc_depfile_tests() {
    # -------------------------------------------------------------------------
    TEST "Dependency file generation"

    generate_code 1 test1.c

    # test compilation without ccache to get a reference file
    $TASKING_COMPILER --dep-file $TASKING_FLAGS -o reference_test1.${TASKING_EXTENSION} test1.c
    expect_exists test1.d
    mv test1.d reference_test1.d

    # First compile with option to generate dependency file
    $CCACHE_TASKING --dep-file $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}
    expect_equal_content test1.d reference_test1.d

    # Second compile with option to generate dependency file
    # remove the previous dependency file
    rm test1.d
    $CCACHE_TASKING --dep-file $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}
    expect_equal_content test1.d reference_test1.d

    # remove cache
    remove_cache

    # First compile with option to generate dependency file
    $CCACHE_TASKING --dep-file=dep_test1.d $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}
    expect_equal_content dep_test1.d reference_test1.d

    # Second compile with option to generate dependency file -> cache hit and generate dependency
    # remove the previous dependency file
    rm dep_test1.d
    $CCACHE_TASKING --dep-file=dep_test1.d $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}
    expect_equal_content dep_test1.d reference_test1.d

    # Third compile with option to generate dependency file with another name -> cache miss
    # remove the previous dependency file
    rm dep_test1.d
    $CCACHE_TASKING --dep-file=otherdep_test1.d $TASKING_FLAGS test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    $TASKING_COMPARE_OUTPUT reference_test1.${TASKING_EXTENSION} test1.${TASKING_EXTENSION}
    expect_equal_content otherdep_test1.d reference_test1.d

    return 0

}

ctc_set_up_different_sets_of_headers_test() {
    BASEDIR=$(pwd)
    BASEDIR1="$BASEDIR/test/dir1"
    BASEDIR2="$BASEDIR/test/dir2"
    BASEDIR3="$BASEDIR/test/dir3"
    BASEDIR4="$BASEDIR/test/dir4"
    BASEDIR5="$BASEDIR/test/dir5"

    mkdir -p $BASEDIR1 $BASEDIR2 $BASEDIR3 $BASEDIR4 $BASEDIR5

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
    cp -f "$BASEDIR1/test.c" $BASEDIR5

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

    cat <<EOF >"$BASEDIR5/header.h"
void test();
EOF


    backdate "$BASEDIR1/header.h" "$BASEDIR1/test.c"
    backdate "$BASEDIR2/header.h" "$BASEDIR2/test.c"
    backdate "$BASEDIR3/header.h" "$BASEDIR3/test.c"
    backdate "$BASEDIR4/header.h" "$BASEDIR4/test.c" "$BASEDIR4/header2.h"
    backdate "$BASEDIR5/header.h" "$BASEDIR5/test.c"
}

ctc_generate_reference_compiler_output() {
    local filename
    if [[ $# -gt 0 ]]
    then
        filename=$1
    else
        filename=test.c
    fi
    rm -f *.${TASKING_EXTENSION} *.d
    $TASKING_COMPILER $TASKING_DEPFLAGS $filename
    mv test.${TASKING_EXTENSION} reference_test.${TASKING_EXTENSION}
    mv test.d reference_test.d
}

ctc_dependmode_set1_tests() {

    # -------------------------------------------------------------------------
    TEST "Basic depend mode"

    unset CCACHE_NODIRECT

    ctc_set_up_different_sets_of_headers_test

    # no dependency file option -> disabling depend mode
    CCACHE_DEPEND=1 $CCACHE_TASKING $TASKING_FLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1
    expect_stat files_in_cache 2 # result + manifest

    # clear the cache for other tests
    clear_cache

    # first run with dependency file -> generates result and manifest
    CCACHE_DEPEND=1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 0
    expect_stat files_in_cache 2 # result + manifest
    expect_exists test.d
    mv test.d reference_test.d

    # second run with dependency file -> uses result and manifest
    CCACHE_DEPEND=1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 0
    expect_stat files_in_cache 2 # result + manifest
    expect_equal_content test.d reference_test.d
    rm test.d

    # third run with dependency file and modify header file content -> generate second entry
    echo "// just another test" >> `pwd`/test/dir1/header.h
    CCACHE_DEPEND=1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat direct_cache_miss 2
    expect_stat preprocessed_cache_miss 0
    expect_stat files_in_cache 3 # result + manifest
    expect_equal_content test.d reference_test.d
    rm test.d


    # clear the cache for other tests
    clear_cache

    # first run with dependency file -> generates result and manifest
    CCACHE_DEPEND=1 $CCACHE_TASKING --dep-file=mysecond.d $TASKING_FLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 0
    expect_stat files_in_cache 2 # result + manifest
    expect_equal_content mysecond.d reference_test.d
    rm mysecond.d

    # second run with dependency file but with different name -> uses result and manifest
    CCACHE_DEPEND=1 $CCACHE_TASKING --dep-file=mysecond.d $TASKING_FLAGS `pwd`/test/dir1/test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 0
    expect_stat files_in_cache 2 # result + manifest
    expect_equal_content mysecond.d reference_test.d

    # -------------------------------------------------------------------------
    TEST "Depend mode with basedir and relative dir"

    unset CCACHE_NODIRECT

    ctc_set_up_different_sets_of_headers_test

    # Compile dir1.
    cd $BASEDIR1

    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir1 first time.
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Compile dir2. dir2 header changes the object file compared to dir1.
    cd $BASEDIR2
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 3      # 2x result, 1x manifest

    # Compile dir3. dir3 header change does not change object file compared to
    # dir1, but ccache still adds an additional .o/.d file in the cache due to
    # different contents of the header file.
    cd $BASEDIR3
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3
    expect_stat files_in_cache 4      # 3x result, 1x manifest

    # Compile dir4. dir4 header adds a new dependency.
    cd $BASEDIR4
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5      # 4x result, 1x manifest

    # Compile dir5. dir5 is identical to dir1
    cd $BASEDIR5
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR5 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_equal_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5      # 4x result, 1x manifest

    # Recompile dir1 second time.
    cd $BASEDIR1
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir2.
    cd $BASEDIR2
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 4
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir3.
    cd $BASEDIR3
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 5
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir4.
    cd $BASEDIR4
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 6
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Compile dir5. dir5 is identical to dir1
    cd $BASEDIR5
    ctc_generate_reference_compiler_output
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR5 $CCACHE_TASKING $TASKING_DEPFLAGS test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_equal_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 7
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # -------------------------------------------------------------------------
    TEST "Depend mode with basedir and absolute dir"

    unset CCACHE_NODIRECT

    ctc_set_up_different_sets_of_headers_test

    # Compile dir1.
    cd $BASEDIR1

    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir1 first time.
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Compile dir2. dir2 header changes the object file compared to dir1.
    cd $BASEDIR2
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 3      # 2x result, 1x manifest

    # Compile dir3. dir3 header change does not change object file compared to
    # dir1, but ccache still adds an additional .o/.d file in the cache due to
    # different contents of the header file.
    cd $BASEDIR3
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3
    expect_stat files_in_cache 4      # 3x result, 1x manifest

    # Compile dir4. dir4 header adds a new dependency.
    cd $BASEDIR4
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5      # 4x result, 1x manifest

    # Compile dir5. dir5 is identical to dir1
    cd $BASEDIR5
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR5 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    # expect_equal_content reference_test.d test.d
    expect_equal_content test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5      # 4x result, 1x manifest

    # Recompile dir1 second time.
    cd $BASEDIR1
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir2.
    cd $BASEDIR2
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 4
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir3.
    cd $BASEDIR3
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR3 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 5
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Recompile dir4.
    cd $BASEDIR4
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR4 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_different_content reference_test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 6
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5

    # Compile dir5. dir5 is identical to dir1
    cd $BASEDIR5
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR5 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    # expect_equal_content reference_test.d test.d
    expect_equal_content test.d $BASEDIR1/test.d
    expect_stat direct_cache_hit 7
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 4
    expect_stat files_in_cache 5      # 4x result, 1x manifest

    # Compile dir5. dir5 is identical to dir1, but modify header file
    cd $BASEDIR5
    echo "// add dummy comment" >> header.h
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR5 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 7
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 5
    expect_stat files_in_cache 6      # 5x result, 1x manifest

    return 0

}


ctc_set_up_different_system_headers_test() {
    BASEDIR=$(pwd)
    BASEDIR1="$BASEDIR/test/dir1"
    BASEDIR2="$BASEDIR/test/dir2"

    mkdir -p $BASEDIR/system1 $BASEDIR/system2 $BASEDIR1 $BASEDIR1/header2 $BASEDIR2 $BASEDIR2/header2

    cat <<EOF >$BASEDIR1/test.c
#include "header1.h"
#include "header2.h"
#include "system1.h"
#include "system2.h"
#include <stdio.h>

void test(){
}
EOF
    cp -f "$BASEDIR1/test.c" $BASEDIR2

    cat <<EOF >"$BASEDIR1/header1.h"
void test();
EOF

    cp -f $BASEDIR1/header1.h $BASEDIR2/header1.h

    cat <<EOF >"$BASEDIR1/header2/header2.h"
// this is a dummy header2 file;
EOF

    cp -f $BASEDIR1/header2/header2.h $BASEDIR2/header2/header2.h

    cat <<EOF >"$BASEDIR/system1/system1.h"
// this is a dummy system file;
EOF

    cp -f $BASEDIR/system1/system1.h $BASEDIR/system2/system2.h


    backdate "$BASEDIR1/header.h" "$BASEDIR1/test.c" "$BASEDIR1/header2/header2.h"
    backdate "$BASEDIR2/header.h" "$BASEDIR2/test.c" "$BASEDIR2/header2/header2.h"
    backdate "$BASEDIR/system1/system1.h"
    backdate "$BASEDIR/system2/system2.h"
}

ctc_dependmode_set2_tests() {

    # -------------------------------------------------------------------------
    TEST "Depend mode with basedir and system headers"

    unset CCACHE_NODIRECT

    ctc_set_up_different_system_headers_test

    # save initial depflags
    local TASKING_DEPFLAGS_ORIG=$TASKING_DEPFLAGS

    # Compile dir1.
    cd $BASEDIR1

    TASKING_DEPFLAGS="$TASKING_DEPFLAGS_ORIG -I $BASEDIR1/header2 -I $BASEDIR/system1 -I $BASEDIR/system2"

    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir1 first time.
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Compile dir2.
    cd $BASEDIR2

    TASKING_DEPFLAGS="$TASKING_DEPFLAGS_ORIG -I $BASEDIR2/header2 -I $BASEDIR/system1 -I $BASEDIR/system2"

    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir2 first time.
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Recompile dir2 second time with a change in the main source file
    echo "// more stuff" >> `pwd`/test.c
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 4 # 2xresult + 2xmanifest

    # Recompile dir2 third time with a change in the system file
    echo "// more stuff" >> $BASEDIR/system1/system1.h
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3
    expect_stat files_in_cache 5 # 2xresult + 3xmanifest

    # -------------------------------------------------------------------------
    TEST "Depend mode with basedir and ignored system headers"

    unset CCACHE_NODIRECT

    ctc_set_up_different_system_headers_test

    # save initial depflags
    local TASKING_DEPFLAGS_ORIG=$TASKING_DEPFLAGS

    # Compile dir1 with a subset of the header files ignored
    cd $BASEDIR1

    TASKING_DEPFLAGS="$TASKING_DEPFLAGS_ORIG -I $BASEDIR1/header2 -I $BASEDIR/system1 -I $BASEDIR/system2"

    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR1 CCACHE_IGNOREHEADERS=$BASEDIR/system1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    expect_equal_content reference_test.d test.d # different because of absolute path
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Compile dir2 a first time, should match the existing
    cd $BASEDIR2

    TASKING_DEPFLAGS="$TASKING_DEPFLAGS_ORIG -I $BASEDIR2/header2 -I $BASEDIR/system1 -I $BASEDIR/system2"

    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 CCACHE_IGNOREHEADERS=$BASEDIR/system1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir2 first time with a change in the header file that is ignored
    echo "// more stuff" >> $BASEDIR/system1/system1.h
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 CCACHE_IGNOREHEADERS=$BASEDIR/system1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2      # result + manifest

    # Recompile dir2 second time with a change in the header file that is NOT ignored
    echo "// more stuff" >> $BASEDIR/system2/system2.h
    ctc_generate_reference_compiler_output `pwd`/test.c
    CCACHE_DEPEND=1 CCACHE_BASEDIR=$BASEDIR2 CCACHE_IGNOREHEADERS=$BASEDIR/system1 $CCACHE_TASKING $TASKING_DEPFLAGS `pwd`/test.c
    $TASKING_COMPARE_OUTPUT reference_test.${TASKING_EXTENSION} test.${TASKING_EXTENSION}
    # from the ccache doc: One known issue is that absolute paths are not reproduced in dependency files
    #expect_equal_content reference_test.d test.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 3      # result + 2xmanifest


    # restore original depflags
    TASKING_DEPFLAGS="$TASKING_DEPFLAGS_ORIG"
    return 0

}


SUITE_ctc_PROBE() {
    ${CURRENT_SUITE}_PROBE
}

SUITE_ctc_SETUP() {
    ${CURRENT_SUITE}_SETUP
}

SUITE_ctc() {
    ${CURRENT_SUITE}_base_tests
    ${CURRENT_SUITE}_basedir_tests
    ${CURRENT_SUITE}_depfile_tests
    ${CURRENT_SUITE}_dependmode_set1_tests
    ${CURRENT_SUITE}_dependmode_set2_tests
}
