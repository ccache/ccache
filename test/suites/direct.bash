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

    $COMPILER -c -Wp,-MD,expected.d test.c
    $COMPILER -c -Wp,-MMD,expected_mmd.d test.c
    rm test.o
}

SUITE_direct() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $COMPILER -c -o reference_test.o test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2 # result + manifest
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_stat files_in_cache 2 # result + manifest
    expect_equal_object_files reference_test.o test.o

    manifest_file=$(find $CCACHE_DIR -name '*M')
    backdate $manifest_file

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 1
    expect_stat preprocessed_cache_miss 1
    expect_stat primary_storage_hit 2 # result + manifest
    expect_stat primary_storage_miss 2 # result + manifest
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_stat files_in_cache 2
    expect_equal_object_files reference_test.o test.o
    expect_newer_than $manifest_file test.c

    # -------------------------------------------------------------------------
    TEST "Corrupt manifest file"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    manifest_file=`find $CCACHE_DIR -name '*M'`
    rm $manifest_file
    touch $manifest_file

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_NODIRECT"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Modified include file"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo "int test3_2;" >>test3.h
    backdate test3.h
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Removed but previously compiled header file"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    rm test3.h
    cat <<EOF >test1.h
// No more include of test3.h
int test1;
EOF
    backdate test1.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Calculation of dependency file names"

    i=0
    for ext in .o .obj "" . .foo.bar; do
        rm -rf testdir
        mkdir testdir

        dep_file=testdir/`echo test$ext | sed 's/\.[^.]*\$//'`.d

        $CCACHE_COMPILE -MD -c test.c -o testdir/test$ext
        expect_stat direct_cache_hit $((3 * i))
        expect_stat cache_miss $((i + 1))
        rm -f $dep_file

        $CCACHE_COMPILE -MD -c test.c -o testdir/test$ext
        expect_stat direct_cache_hit $((3 * i + 1))
        expect_stat cache_miss $((i + 1))
        expect_exists $dep_file
        if ! grep "test$ext:" $dep_file >/dev/null 2>&1; then
            test_failed "$dep_file does not contain \"test$ext:\""
        fi

        dep_target=foo.bar
        $CCACHE_COMPILE -MD -MQ $dep_target -c test.c -o testdir/test$ext
        expect_stat direct_cache_hit $((3 * i + 1))
        expect_stat cache_miss $((i + 2))
        rm -f $dep_target

        $CCACHE_COMPILE -MD -MQ $dep_target -c test.c -o testdir/test$ext
        expect_stat direct_cache_hit $((3 * i + 2))
        expect_stat cache_miss $((i + 2))
        expect_exists $dep_file
        if ! grep $dep_target $dep_file >/dev/null 2>&1; then
            test_failed "$dep_file does not contain $dep_target"
        fi

        i=$((i + 1))
    done
    expect_stat files_in_cache $((2 * i + 2))

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    backdate a/source.h b/source.h
    $CCACHE_COMPILE -MMD -c a/source.c -o a/source.o
    expect_content a/source.d "a/source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c -o b/source.o
    expect_content b/source.d "b/source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c -o a/source.o
    expect_content a/source.d "a/source.o: a/source.c"

    # -------------------------------------------------------------------------
    for dep_args in "-MMD" "-MMD -MF foo.d" "-Wp,-MMD,foo.d"; do
        for obj_args in "" "-o bar.o"; do
            if [ "$dep_args" != "-MMD" ]; then
                dep_file=foo.d
                another_dep_file=foo.d
            elif [ -z "$obj_args" ]; then
                dep_file=test.d
                another_dep_file=another.d
            else
                dep_file=bar.d
                another_dep_file=another.d
            fi

            TEST "Dependency file content, $dep_args $obj_args"
            # -----------------------------------------------------------------

            $COMPILER -c test.c $dep_args $obj_args
            mv $dep_file $dep_file.real

            $COMPILER -c test.c $dep_args -o another.o
            mv $another_dep_file another.d.real

            # cache miss
            $CCACHE_COMPILE -c test.c $dep_args $obj_args
            expect_stat direct_cache_hit 0
            expect_stat cache_miss 1
            expect_equal_content $dep_file.real $dep_file

            # cache hit
            $CCACHE_COMPILE -c test.c $dep_args $obj_args
            expect_stat direct_cache_hit 1
            expect_stat cache_miss 1
            expect_equal_content $dep_file.real $dep_file

            # change object file name
            $CCACHE_COMPILE -c test.c $dep_args -o another.o
            expect_equal_content another.d.real $another_dep_file
        done
    done

    # -------------------------------------------------------------------------
    TEST "-MD/-MMD dependency target rewriting"

    touch test1.c

    for option in -MD -MMD; do
        $CCACHE -z >/dev/null
        i=0
        for dir in build1 build2 dir/dir2/dir3; do
            mkdir -p $dir
            for name in test1 obj1 random2; do
                obj=$dir/$name.o
                dep=$(echo $obj | sed 's/\.o$/.d/')

                $COMPILER $option -c test1.c -o $obj
                mv $dep orig.d

                $CCACHE_COMPILE $option -c test1.c -o $obj
                diff -u orig.d $dep
                expect_equal_content $dep orig.d
                expect_stat direct_cache_hit $i
                expect_stat cache_miss 1

                i=$((i + 1))
            done
            rm -rf $dir
        done
    done

    # -------------------------------------------------------------------------
    TEST "-MMD: cache hits and miss and dependency"

    hit=0
    src=test2.c
    touch $src
    orig_dep=orig.d
    for dir1 in build1 build2 dir1/dir2/dir3; do
        mkdir -p $dir1
        for name in test2 obj1 obj2; do
            obj=$dir1/$name.o
            dep=$(echo $obj | sed 's/\.o$/.d/')
            $COMPILER -MMD -c $src -o $obj
            mv $dep $orig_dep
            rm $obj

            $CCACHE_COMPILE -MMD -c $src -o $obj
            dep=$(echo $obj | sed 's/\.o$/.d/')
            expect_content $dep "$obj: $src"
            expect_stat direct_cache_hit $hit
            expect_stat cache_miss 1
            hit=$((hit + 1))

            rm $orig_dep
        done
        rm -rf $dir1
    done

    # -------------------------------------------------------------------------
    TEST "Dependency file content"

    mkdir build
    touch test1.c
    cp test1.c build

    for src in test1.c build/test1.c; do
        for obj in test1.o build/test1.o; do
            $CCACHE_COMPILE -c -MMD $src -o $obj
            dep=$(echo $obj | sed 's/\.o$/.d/')
            expect_content $dep "$obj: $src"
        done
    done

    # -------------------------------------------------------------------------
    TEST "-MMD for different include file paths"

    mkdir a b
    touch a/source.h b/source.h
    backdate a/source.h b/source.h
    echo '#include <source.h>' >source.c
    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_content source.d "source.o: source.c a/source.h"

    $CCACHE_COMPILE -MMD -Ib -c source.c
    expect_content source.d "source.o: source.c b/source.h"

    $CCACHE_COMPILE -MMD -Ia -c source.c
    expect_content source.d "source.o: source.c a/source.h"

    # -------------------------------------------------------------------------
    TEST "-Wp,-MD"

    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected.d

    $COMPILER -c -Wp,-MD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MD,other.d test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MD,different_name.d test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-MMD"

    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected_mmd.d

    $COMPILER -c -Wp,-MMD,other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d
    $CCACHE_COMPILE -c -Wp,-MMD,other.d test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -Wp,-MMD,different_name.d test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content different_name.d expected_mmd.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -DFOO test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-Wp, with multiple arguments"

    # ccache could try to parse and make sense of -Wp, with multiple arguments,
    # but it currently doesn't, so we have to disable direct mode.

    touch source.c

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content source.d "source.o: source.c"

    $CCACHE_COMPILE -c -Wp,-MMD,source.d,-MT,source.o source.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content source.d "source.o: source.c"

    # -------------------------------------------------------------------------
    TEST "-MMD for different source files"

    mkdir a b
    touch a/source.c b/source.c
    $CCACHE_COMPILE -MMD -c a/source.c
    expect_content source.d "source.o: a/source.c"

    $CCACHE_COMPILE -MMD -c b/source.c
    expect_content source.d "source.o: b/source.c"

    $CCACHE_COMPILE -MMD -c a/source.c
    expect_content source.d "source.o: a/source.c"

    # -------------------------------------------------------------------------
    TEST "Multiple object entries in manifest"

    for i in 0 1 2 3 4; do
        echo "int test1_$i;" >>test1.h
        backdate test1.h
        $CCACHE_COMPILE -c test.c
        $CCACHE_COMPILE -c test.c
    done
    expect_stat direct_cache_hit 5
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 5

    # -------------------------------------------------------------------------
    TEST "-MD"

    $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d

    $COMPILER -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d
    $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage"

    cat <<EOF >code.c
int test() { return 0; }
EOF

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists code.gcno

    rm code.gcno

    $CCACHE_COMPILE -c -fprofile-arcs -ftest-coverage code.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists code.gcno

    # -------------------------------------------------------------------------
    TEST "-fstack-usage"

    cat <<EOF >code.c
int test() { return 0; }
EOF

    if $COMPILER -c -fstack-usage code.c >/dev/null 2>&1; then
        $CCACHE_COMPILE -c -fstack-usage code.c
        expect_stat direct_cache_hit 0
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1
        expect_exists code.su

        rm code.su

        $CCACHE_COMPILE -c -fstack-usage code.c
        expect_stat direct_cache_hit 1
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1
        expect_exists code.su
    fi

    # -------------------------------------------------------------------------
    TEST "Direct mode on cache created by ccache without direct mode support"

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d
    $COMPILER -c -MD test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f test.d

    $CCACHE_COMPILE -c -MD test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_equal_content test.d expected.d
    expect_equal_object_files reference_test.o test.o

    # -------------------------------------------------------------------------
    TEST "-MF"

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected.d
    $COMPILER -c -MD -MF other.d test.c -o reference_test.o
    expect_equal_object_files reference_test.o test.o

    rm -f other.d

    $CCACHE_COMPILE -c -MD -MF other.d test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content other.d expected.d
    expect_equal_object_files reference_test.o test.o

    $CCACHE_COMPILE -c -MD -MF different_name.d test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content different_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f different_name.d

    $CCACHE_COMPILE -c -MD -MFthird_name.d test.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content third_name.d expected.d
    expect_equal_object_files reference_test.o test.o

    rm -f third_name.d

    # -------------------------------------------------------------------------
    TEST "MF /dev/null"

    $CCACHE_COMPILE -c -MD -MF /dev/null test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # result + manifest

    $CCACHE_COMPILE -c -MD -MF /dev/null test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    $CCACHE_COMPILE -c -MD -MF test.d test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_equal_content test.d expected.d

    rm -f test.d

    $CCACHE_COMPILE -c -MD -MF test.d test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_equal_content test.d expected.d

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-cpp.txt
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content stderr-cpp.txt "`cat stderr-orig.txt`"

    $CCACHE_COMPILE -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content stderr-mf.txt "`cat stderr-orig.txt`"

    # -------------------------------------------------------------------------
    TEST "Empty source file"

    touch empty.c

    $CCACHE_COMPILE -c empty.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c empty.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Empty include file"

    touch empty.h
    cat <<EOF >include_empty.c
#include "empty.h"
EOF
    backdate empty.h
    $CCACHE_COMPILE -c include_empty.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    $CCACHE_COMPILE -c include_empty.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "The source file path is included in the hash"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF
    cp file.c file2.c

    $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c file2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c file2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c $(pwd)/file.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3

    $CCACHE_COMPILE -c $(pwd)/file.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "The source file path is included even if sloppiness = file_macro"

    cat <<EOF >file.c
#define file __FILE__
int test;
EOF
    cp file.c file2.c

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c file2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c $(pwd)/file.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS file_macro" $CCACHE_COMPILE -c $(pwd)/file.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "Relative includes for identical source code in different directories"

    mkdir a
    cat <<EOF >a/file.c
#include "file.h"
EOF
    cat <<EOF >a/file.h
int x = 1;
EOF
    backdate a/file.h

    mkdir b
    cat <<EOF >b/file.c
#include "file.h"
EOF
    cat <<EOF >b/file.h
int x = 2;
EOF
    backdate b/file.h

    $CCACHE_COMPILE -c a/file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c a/file.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c b/file.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c b/file.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file disables direct mode"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    $CCACHE_COMPILE -c time.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c time.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c time_h.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c time_h.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "__TIME__ in source file ignored if sloppy"

    cat <<EOF >time.c
#define time __TIME__
int test;
EOF

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE -c time_h.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c new.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "__DATE__ in header file results in direct cache hit as the date remains the same"

    cat <<EOF >test_date2.c
// test_date2.c
#include "test_date2.h"
char date_str[] = MACRO_STRING;
EOF
    cat <<EOF >test_date2.h
#define MACRO_STRING __DATE__
EOF

    backdate test_date2.c test_date2.h

    $CCACHE_COMPILE -MP -MMD -MF test_date2.d -c test_date2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -MP -MMD -MF test_date2.d -c test_date2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime" $CCACHE_COMPILE -c new.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Sloppy Clang index store"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_index_store" $CCACHE_COMPILE -index-store-path foo -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_index_store" $CCACHE_COMPILE -index-store-path bar -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

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
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CPATH=subdir1 $CCACHE_COMPILE -c foo.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2 # subdir2 is part of the preprocessor output

    CPATH=subdir2 $CCACHE_COMPILE -c foo.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Comment in strings"

    echo 'const char *comment = " /* \\\\u" "foo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c comment.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo 'const char *comment = " /* \\\\u" "goo" " */";' >comment.c

    $CCACHE_COMPILE -c comment.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

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

    manifest=`find $CCACHE_DIR -name '*M'`
    if [ -n "$manifest" ]; then
        data="`$CCACHE --dump-manifest $manifest | egrep '/dev/(stdout|tty|sda|hda'`"
        if [ -n "$data" ]; then
            test_failed "$manifest contained troublesome file(s): $data"
        fi
    fi

    # -------------------------------------------------------------------------
    TEST "--dump-manifest"

    $CCACHE_COMPILE test.c -c -o test.o

    manifest=`find $CCACHE_DIR -name '*M'`
    $CCACHE --dump-manifest $manifest >manifest.dump

    checksum_test1_h='b7273h0ksdehi0o4pitg5jeehal3i54ns'
    checksum_test2_h='24f1315jch5tcndjbm6uejtu8q3lf9100'
    checksum_test3_h='56a6dkffffv485aepk44seaq3i6lbepq2'

    if grep "Hash: $checksum_test1_h" manifest.dump >/dev/null 2>&1 && \
       grep "Hash: $checksum_test2_h" manifest.dump >/dev/null 2>&1 && \
       grep "Hash: $checksum_test3_h" manifest.dump >/dev/null 2>&1; then
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
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -B -L -DBAR -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

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
    manifest=`find $CCACHE_DIR -name '*M'`
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
    manifest=`find $CCACHE_DIR -name '*M'`
    data="`$CCACHE --dump-manifest $manifest | grep subdir/ignore.h`"
    if [ -n "$data" ]; then
        test_failed "$manifest contained ignored header: $data"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_IGNOREOPTIONS"

    CCACHE_IGNOREOPTIONS="-DTEST=1" $CCACHE_COMPILE -DTEST=1 -c test.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_IGNOREOPTIONS="-DTEST=1*" $CCACHE_COMPILE -DTEST=1 -c test.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_IGNOREOPTIONS="-DTEST=1*" $CCACHE_COMPILE -DTEST=12 -c test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_IGNOREOPTIONS="-DTEST=2*" $CCACHE_COMPILE -DTEST=12 -c test.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE doesn't add a new manifest entry"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat recache 0
    expect_stat files_in_cache 2 # result + manifest

    manifest_file=$(find $CCACHE_DIR -name '*M')
    cp $manifest_file saved.manifest

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat recache 1
    expect_stat files_in_cache 2

    expect_equal_content $manifest_file saved.manifest
}
