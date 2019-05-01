SUITE_direct_SETUP() {
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
}

SUITE_direct() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $REAL_COMPILER -c -o reference_test.o test.c

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # .o + .manifest
    expect_equal_object_files reference_test.o test.o

    manifest_file=$(find $CCACHE_DIR -name '*.manifest')
    backdate $manifest_file

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test.o test.o
    expect_file_newer_than $manifest_file test.c

    # -------------------------------------------------------------------------
    TEST "Corrupt manifest file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    manifest_file=`find $CCACHE_DIR -name '*.manifest'`
    rm $manifest_file
    touch $manifest_file

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_NODIRECT"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Modified include file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "int test3_2;" >>test3.h
    backdate test3.h
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Removed but previously compiled header file"

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    rm test3.h
    cat <<EOF >test1.h
// No more include of test3.h
int test1;
EOF
    backdate test1.h

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Calculation of dependency file names"

    mkdir test.dir
    for ext in .o .obj "" . .foo.bar; do
        dep_file=test.dir/`echo test$ext | sed 's/\.[^.]*\$//'`.d

        $CCACHE_COMPILE -MD -c test.c -o test.dir/test$ext
        rm -f $dep_file
        $CCACHE_COMPILE -MD -c test.c -o test.dir/test$ext
        expect_file_exists $dep_file
        if ! grep "test$ext:" $dep_file >/dev/null 2>&1; then
            test_failed "$dep_file does not contain test$ext"
        fi

        dep_target=foo.bar
        $CCACHE_COMPILE -MD -MQ $dep_target -c test.c -o test.dir/test$ext
        rm -f $dep_target
        $CCACHE_COMPILE -MD -MQ $dep_target -c test.c -o test.dir/test$ext
        expect_file_exists $dep_file
        if ! grep $dep_target $dep_file >/dev/null 2>&1; then
            test_failed "$dep_file does not contain $dep_target"
        fi
    done
    expect_stat 'files in cache' 18

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    backdate a/source.h b/source.h
    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c
    expect_file_content source.d "source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    # -------------------------------------------------------------------------
    TEST "-MMD for different include file paths"

    mkdir a b
    touch a/source.h b/source.h
    backdate a/source.h b/source.h
    echo '#include <source.h>' >source.c
    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_file_content source.d "source.o: source.c a/source.h"

    $CCACHE_COMPILE -MMD -Ib -c source.c
    expect_file_content source.d "source.o: source.c b/source.h"

    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_file_content source.d "source.o: source.c a/source.h"

    # -------------------------------------------------------------------------
    TEST "-Wp,-MD"

    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d

    $REAL_COMPILER -c -Wp,-MD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MD,different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-MMD"

    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_mmd.d

    $REAL_COMPILER -c -Wp,-MMD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MMD,different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -DFOO test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-Wp, with multiple arguments"

    # ccache could try to parse and make sense of -Wp, with multiple arguments,
    # but it currently doesn't, so we have to disable direct mode.

    touch source.c

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content source.d "source.o: source.c"

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content source.d "source.o: source.c"

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c
    expect_file_content source.d "source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c
    expect_file_content source.d "source.o: a/source.c"

    # -------------------------------------------------------------------------
    TEST "Multiple object entries in manifest"

    for i in 0 1 2 3 4; do
        echo "int test1_$i;" >>test1.h
        backdate test1.h
        $CCACHE_COMPILE -c test.c
        $CCACHE_COMPILE -c test.c
    done
    expect_stat 'cache hit (direct)' 5
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 5

    # -------------------------------------------------------------------------
    TEST "-MD"

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d

    $REAL_COMPILER -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d
    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage"

    cat <<EOF >code.c
int test() { return 0; }
EOF

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists code.gcno

    rm code.gcno

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists code.gcno

    # -------------------------------------------------------------------------
    TEST "-fstack-usage"

    cat <<EOF >code.c
int test() { return 0; }
EOF

    if $COMPILER_TYPE_GCC; then
        $CCACHE_COMPILE -c -fstack-usage code.c
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        expect_file_exists code.su

        rm code.su

        $CCACHE_COMPILE -c -fstack-usage code.c
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        expect_file_exists code.su
    fi

    # -------------------------------------------------------------------------
    TEST "Direct mode on cache created by ccache without direct mode support"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    $REAL_COMPILER -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-MF"

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    $REAL_COMPILER -c -MD -MF other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -MD -MF different_name.d test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f different_name.d

    $CCACHE_COMPILE -c -MD -MFthird_name.d test.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files third_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f third_name.d

    # -------------------------------------------------------------------------
    TEST "MF /dev/null"

    $CCACHE_COMPILE -c -MD -MF /dev/null test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2 # .o + .manifest

    $CCACHE_COMPILE -c -MD -MF /dev/null test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "Missing .d file"

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_files test.d expected.d

    find $CCACHE_DIR -name '*.d' -exec rm '{}' +

    # Missing file -> consider the cached result broken.
    $CCACHE_COMPILE -c -MD test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'cache file missing' 1

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
    $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-orig.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-cpp.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content stderr-cpp.txt "`cat stderr-orig.txt`"

    $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content stderr-mf.txt "`cat stderr-orig.txt`"

    # -------------------------------------------------------------------------
    TEST "Empty source file"

    touch empty.c

    $CCACHE_COMPILE -c empty.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c empty.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Empty include file"

    touch empty.h
    cat <<EOF >include_empty.c
#include "empty.h"
EOF
    backdate empty.h
    $CCACHE_COMPILE -c include_empty.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c include_empty.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__FILE__ in source file disables direct mode"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF

    $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c `pwd`/file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "__FILE__ in include file disables direct mode"

    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF

    $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mv file_h.c file2_h.c

    $CCACHE_COMPILE -c `pwd`/file2_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "__FILE__ in source file ignored if sloppy"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c `pwd`/file.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__FILE__ in include file ignored if sloppy"

    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mv file_h.c file2_h.c

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c `pwd`/file2_h.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file disables direct mode"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in include file disables direct mode"

    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h

    cat <<EOF >time_h.c
#include "time.h"
EOF

    $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file ignored if sloppy"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in include file ignored if sloppy"

    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h
    cat <<EOF >time_h.c
#include "time.h"
EOF
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time_h.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Too new include file disables direct mode"

    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h

    $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "New include file ignored if sloppy"

    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c new.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Sloppy Clang index store"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_index_store" $CCACHE_COMPILE -index-store-path foo -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_index_store" $CCACHE_COMPILE -index-store-path bar -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    # Check that environment variables that affect the preprocessor are taken
    # into account.
    TEST "CPATH included in hash"

    rm -rf subdir1 subdir2
    mkdir subdir1 subdir2
    cat <<EOF >subdir1/foo.h
int foo;
EOF
    cat <<EOF >subdir2/foo.h
int foo;
EOF
    cat <<EOF >foo.c
#include <foo.h>
EOF
    backdate subdir1/foo.h subdir2/foo.h

    CPATH=subdir1 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CPATH=subdir1 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2 # subdir2 is part of the preprocessor output

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Comment in strings"

    echo 'char *comment = " /* \\\\u" "foo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo 'char *comment = " /* \\\\u" "goo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "#line directives with troublesome files"

    cat <<EOF >strange.c
int foo;
EOF
    for x in stdout tty sda hda; do
        if [ -b /dev/$x ] || [ -c /dev/$x ]; then
            echo "#line 1 \"/dev/$x\"" >>strange.c
        fi
    done

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c strange.c

    manifest=`find $CCACHE_DIR -name '*.manifest'`
    if [ -n "$manifest" ]; then
        data="`$CCACHE --dump-manifest $manifest | egrep '/dev/(stdout|tty|sda|hda'`"
        if [ -n "$data" ]; then
            test_failed "$manifest contained troublesome file(s): $data"
        fi
    fi

    # -------------------------------------------------------------------------
    TEST "--dump-manifest"

    $CCACHE_COMPILE test.c -c -o test.o

    manifest=`find $CCACHE_DIR -name '*.manifest'`
    $CCACHE --dump-manifest $manifest >manifest.dump

    if grep 'Hash: d4de2f956b4a386c6660990a7a1ab13f' manifest.dump >/dev/null 2>&1 && \
       grep 'Hash: e94ceb9f1b196c387d098a5f1f4fe862' manifest.dump >/dev/null 2>&1 && \
       grep 'Hash: ba753bebf9b5eb99524bb7447095e2e6' manifest.dump >/dev/null 2>&1; then
        : OK
    else
        test_failed "Unexpected output of --dump-manifest"
    fi

    # -------------------------------------------------------------------------
    TEST "Argument-less -B and -L"

    cat <<EOF >test.c
#include <stdio.h>
int main(void)
{
#ifdef FOO
    puts("FOO");
#endif
    return 0;
}
EOF

    $CCACHE_COMPILE -B -L -DFOO -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -B -L -DBAR -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_IGNOREHEADERS with filename"

    mkdir subdir
    cat <<EOF >subdir/ignore.h
// We don't want this header in the manifest.
EOF
    backdate subdir/ignore.h
    cat <<EOF >ignore.c
#include "subdir/ignore.h"
int foo;
EOF

    CCACHE_IGNOREHEADERS="subdir/ignore.h" $CCACHE_COMPILE -c ignore.c
    manifest=`find $CCACHE_DIR -name '*.manifest'`
    data="`$CCACHE --dump-manifest $manifest | grep subdir/ignore.h`"
    if [ -n "$data" ]; then
        test_failed "$manifest contained ignored header: $data"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_IGNOREHEADERS with directory"

    mkdir subdir
    cat <<EOF >subdir/ignore.h
// We don't want this header in the manifest.
EOF
    backdate subdir/ignore.h
    cat <<EOF >ignore.c
#include "subdir/ignore.h"
int foo;
EOF

    CCACHE_IGNOREHEADERS="subdir" $CCACHE_COMPILE -c ignore.c
    manifest=`find $CCACHE_DIR -name '*.manifest'`
    data="`$CCACHE --dump-manifest $manifest | grep subdir/ignore.h`"
    if [ -n "$data" ]; then
        test_failed "$manifest contained ignored header: $data"
    fi
}
