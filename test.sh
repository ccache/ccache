#!/bin/sh

# a simple test suite for ccache
# tridge@samba.org

if test -n "$CC"; then
    COMPILER="$CC"
else
    COMPILER=cc
fi

CCACHE=$PWD/ccache
TESTDIR=testdir.$$

unset CCACHE_DISABLE

test_failed() {
    echo "SUITE: \"$testsuite\", TEST: \"$testname\" - $1"
    $CCACHE -s
    cd ..
    rm -rf $TESTDIR
    echo TEST FAILED
    exit 1
}

randcode() {
    outfile="$1"
    nlines=$2
    i=0;
    (
    while [ $i -lt $nlines ]; do
        echo "int foo$nlines$i(int x) { return x; }"
        i=`expr $i + 1`
    done
    ) >> "$outfile"
}


getstat() {
    stat="$1"
    value=`$CCACHE -s | grep "$stat" | cut -c34-40`
    echo $value
}

checkstat() {
    stat="$1"
    expected_value="$2"
    value=`getstat "$stat"`
    if [ "$expected_value" != "$value" ]; then
        test_failed "Expected $stat to be $expected_value, got $value"
    fi
}

checkfile() {
    if [ ! -f $1 ]; then
        test_failed "$1 not found"
    fi
    if [ "`cat $1`" != "$2" ]; then
        test_failed "Bad content of $1.\nExpected: $2\nActual: `cat $1`"
    fi
}

run_suite() {
    echo "starting testsuite $1"
    testsuite=$1
    ${1}_suite
}

base_tests() {
    rm -rf $CCACHE_DIR
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0
    checkstat 'files in cache' 0

    j=1
    rm -f *.c
    while [ $j -lt 32 ]; do
        randcode test$j.c $j
        j=`expr $j + 1`
    done

    testname="BASIC"
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkstat 'files in cache' 1

    testname="BASIC2"
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    checkstat 'files in cache' 1

    testname="debug"
    $CCACHE_COMPILE -c test1.c -g
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2
    checkstat 'files in cache' 2

    testname="debug2"
    $CCACHE_COMPILE -c test1.c -g
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 2

    testname="output"
    $CCACHE_COMPILE -c test1.c -o foo.o
    checkstat 'cache hit (preprocessed)' 3
    checkstat 'cache miss' 2

    testname="link"
    $CCACHE_COMPILE test1.c -o test 2> /dev/null
    checkstat 'called for link' 1

    testname="multiple"
    $CCACHE_COMPILE -c test1.c test2.c
    checkstat 'multiple source files' 1

    testname="find"
    $CCACHE blahblah -c test1.c 2> /dev/null
    checkstat "couldn't find the compiler" 1

    testname="bad"
    $CCACHE_COMPILE -c test1.c -I 2> /dev/null
    checkstat 'bad compiler arguments' 1

    testname="c/c++"
    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2> /dev/null
    checkstat 'not a C/C++ file' 1

    testname="unsupported"
    $CCACHE_COMPILE -M foo -c test1.c > /dev/null 2>&1
    checkstat 'unsupported compiler option' 1

    testname="stdout"
    $CCACHE echo foo -c test1.c > /dev/null
    checkstat 'compiler produced stdout' 1

    testname="non-regular"
    mkdir testd
    $CCACHE_COMPILE -o testd -c test1.c > /dev/null 2>&1
    rmdir testd
    checkstat 'output to a non-regular file' 1

    testname="no-input"
    $CCACHE_COMPILE -c -O2 2> /dev/null
    checkstat 'no input file' 1

    testname="CCACHE_DISABLE"
    CCACHE_DISABLE=1 $CCACHE_COMPILE -c test1.c 2> /dev/null
    checkstat 'cache hit (preprocessed)' 3
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 4

    testname="CCACHE_CPP2"
    CCACHE_CPP2=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 4
    checkstat 'cache miss' 3

    CCACHE_CPP2=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 5
    checkstat 'cache miss' 3

    testname="CCACHE_NOSTATS"
    CCACHE_NOSTATS=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 5
    checkstat 'cache miss' 3

    testname="CCACHE_RECACHE"
    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 5
    checkstat 'cache miss' 4

    # strictly speaking should be 3 - RECACHE causes a double counting!
    checkstat 'files in cache' 4
    $CCACHE -c > /dev/null
    checkstat 'files in cache' 3

    testname="CCACHE_HASHDIR"
    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 5
    checkstat 'cache miss' 5

    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 6
    checkstat 'cache miss' 5
    checkstat 'files in cache' 4

    testname="comments"
    echo '/* a silly comment */' > test1-comment.c
    cat test1.c >> test1-comment.c
    $CCACHE_COMPILE -c test1-comment.c
    rm -f test1-comment*
    checkstat 'cache hit (preprocessed)' 6
    checkstat 'cache miss' 6

    testname="CCACHE_UNIFY"
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 6
    checkstat 'cache miss' 7
    mv test1.c test1-saved.c
    echo '/* another comment */' > test1.c
    cat test1-saved.c >> test1.c
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    mv test1-saved.c test1.c
    checkstat 'cache hit (preprocessed)' 7
    checkstat 'cache miss' 7

    testname="cache-size"
    for f in *.c; do
        $CCACHE_COMPILE -c $f
    done
    checkstat 'cache hit (preprocessed)' 8
    checkstat 'cache miss' 37
    checkstat 'files in cache' 36

    $CCACHE -F 32 -c > /dev/null
    if [ `getstat 'files in cache'` -gt 32 ]; then
        test_failed '-F test failed'
    fi

    testname="cpp call"
    $CCACHE_COMPILE -c test1.c -E > test1.i
    checkstat 'cache hit (preprocessed)' 8
    checkstat 'cache miss' 37

    testname="direct .i compile"
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 8
    checkstat 'cache miss' 38

    $CCACHE_COMPILE -c test1.i
    checkstat 'cache hit (preprocessed)' 9
    checkstat 'cache miss' 38

    $CCACHE_COMPILE -c test1.i
    checkstat 'cache hit (preprocessed)' 10
    checkstat 'cache miss' 38

    # removed these tests as some compilers (including newer versions of gcc)
    # determine which language to use based on .ii/.i extension, and C++ may
    # not be installed
#     testname="direct .ii file"
#     mv test1.i test1.ii
#     $CCACHE_COMPILE -c test1.ii
#     checkstat 'cache hit (preprocessed)' 10
#     checkstat 'cache miss' 39

#     $CCACHE_COMPILE -c test1.ii
#     checkstat 'cache hit (preprocessed)' 11
#     checkstat 'cache miss' 39

    testname="stderr-files"
    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 0 ]; then
        test_failed "$num stderr files found, expected 0"
    fi
    cat <<EOF >stderr.c
int stderr(void)
{
	/* Trigger warning by having no return statement. */
}
EOF
    $CCACHE_COMPILE -Wall -W -c stderr.c 2>/dev/null
    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 1 ]; then
        test_failed "$num stderr files found, expected 1"
    fi

    testname="zero-stats"
    $CCACHE -z > /dev/null
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0

    testname="clear"
    $CCACHE -C > /dev/null
    checkstat 'files in cache' 0

    rm -f test1.c
}

base_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    base_tests
}

link_suite() {
    ln -s ../ccache $COMPILER
    CCACHE_COMPILE="./$COMPILER"
    base_tests
}

hardlink_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    CCACHE_HARDLINK=1
    export CCACHE_HARDLINK
    base_tests
    unset CCACHE_HARDLINK
}

cpp2_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    CCACHE_CPP2=1
    export CCACHE_CPP2
    base_tests
    unset CCACHE_CPP2
}

nlevels4_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    CCACHE_NLEVELS=4
    export CCACHE_NLEVELS
    base_tests
    unset CCACHE_NLEVELS
}

nlevels1_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    CCACHE_NLEVELS=1
    export CCACHE_NLEVELS
    base_tests
    unset CCACHE_NLEVELS
}

direct_suite() {
    rm -rf $CCACHE_DIR
    unset CCACHE_NODIRECT

    ##################################################################
    # Create some code to compile.
    cat <<EOF >test.c
/* test.c */
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

    sleep 1 # Sleep to make the include files trusted.

    ##################################################################
    # First compilation is a miss.
    testname="first compilation"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Another compilation should now generate a direct hit.
    testname="direct hit"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0

    ##################################################################
    # Compiling with CCACHE_NODIRECT set should generate a preprocessed hit.
    testname="preprocessed hit"
    $CCACHE -z >/dev/null
    CCACHE_NODIRECT=1 $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 0

    ##################################################################
    # Test compilation of a modified include file.
    testname="modified include file"
    $CCACHE -z >/dev/null
    echo "int test3_2;" >>test3.h
    sleep 1 # Sleep to make the include file trusted.
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # A removed but previously compiled header file should be handled
    # gracefully.
    testname="missing header file"
    $CCACHE -z >/dev/null

    mv test1.h test1.h.saved
    mv test3.h test3.h.saved
    cat <<EOF >test1.h
/* No more include of test3.h */
int test1;
EOF
    sleep 1 # Sleep to make the include file trusted.

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    # Restore
    mv test1.h.saved test1.h
    mv test3.h.saved test3.h
    sleep 1 # Sleep to make the include files trusted.

    rm -f other.d

    ##################################################################
    # Check that -Wp,-MD,file.d works.
    testname="-Wp,-MD"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -Wp,-MD,other.d test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    rm -f other.d

    $CCACHE $COMPILER -c -Wp,-MD,other.d test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    rm -f other.d

    ##################################################################
    # Check that -Wp,-MMD,file.d works.
    testname="-Wp,-MMD"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -Wp,-MMD,other.d test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    rm -f other.d

    $CCACHE $COMPILER -c -Wp,-MMD,other.d test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    rm -f other.d

    ##################################################################
    # Test some header modifications to get multiple objects in the manifest.
    testname="several objects"
    $CCACHE -z >/dev/null
    for i in 0 1 2 3 4; do
        echo "int test1_$i;" >>test1.h
        sleep 1 # Sleep to make the include file trusted.
        $CCACHE $COMPILER -c test.c
        $CCACHE $COMPILER -c test.c
    done
    checkstat 'cache hit (direct)' 5
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 5

    ##################################################################
    # Check that -MD works.
    testname="-MD"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    rm -f test.d

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    ##################################################################
    # Check the scenario of running a ccache with direct mode on a cache
    # built up by a ccache without direct mode support.
    testname="direct mode on old cache"
    $CCACHE -z >/dev/null
    $CCACHE -C >/dev/null
    CCACHE_NODIRECT=1 $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    rm -f test.d

    CCACHE_NODIRECT=1 $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    rm -f test.d

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    rm -f test.d

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 1
    checkfile test.d "test.o: test.c test1.h test3.h test2.h"

    ##################################################################
    # Check that -MF works.
    testname="-MF"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -MD -MF other.d test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    rm -f other.d

    $CCACHE $COMPILER -c -MD -MF other.d test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    ##################################################################
    # Check that a missing .d file in the cache is handled correctly.
    testname="missing dependency file"
    $CCACHE -z >/dev/null
    $CCACHE -C >/dev/null

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    find $CCACHE_DIR -name '*.d' -exec rm -f '{}' \;

    $CCACHE $COMPILER -c -MD test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    checkfile other.d "test.o: test.c test1.h test3.h test2.h"

    ##################################################################
    # Check that stderr from both the preprocessor and the compiler is emitted
    # in direct mode too.
    testname="cpp stderr"
    $CCACHE -z >/dev/null
    $CCACHE -C >/dev/null
cat <<EOF >cpp-warning.c
#if FOO
/* Trigger preprocessor warning about extra token after #endif. */
#endif FOO
int stderr(void)
{
	/* Trigger compiler warning by having no return statement. */
}
EOF
    $CCACHE $COMPILER -Wall -W -c cpp-warning.c 2>stderr-orig.txt
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    CCACHE_NODIRECT=1 $CCACHE $COMPILER -Wall -W -c cpp-warning.c 2>stderr-cpp.txt
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    checkfile stderr-cpp.txt "`cat stderr-orig.txt`"

    $CCACHE $COMPILER -Wall -W -c cpp-warning.c 2>stderr-mf.txt
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    checkfile stderr-mf.txt "`cat stderr-orig.txt`"

    ##################################################################
    # Reset things.
    CCACHE_NODIRECT=1
    export CCACHE_NODIRECT
    $CCACHE -C >/dev/null
}

basedir_suite() {
    rm -rf $CCACHE_DIR

    ##################################################################
    # Create some code to compile.
    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2

    cat <<EOF >stderr.h
int stderr(void)
{
	/* Trigger warning by having no return statement. */
}
EOF
    cat <<EOF >stderr.c
#include <stderr.h>
EOF

    sleep 1 # Sleep to make the include files trusted.

    ##################################################################
    # CCACHE_BASEDIR="" and using absolute include path will result in a cache
    # miss.
    testname="empty CCACHE_BASEDIR"
    $CCACHE -z >/dev/null

    cd dir1
    CCACHE_BASEDIR="" $CCACHE $COMPILER -I$PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    CCACHE_BASEDIR="" $CCACHE $COMPILER -I$PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2
    cd ..

    ##################################################################
    # Setting CCACHE_BASEDIR will result in a cache hit because include paths
    # in the preprocessed output are rewritten.
    testname="set CCACHE_BASEDIR"
    $CCACHE -z >/dev/null
    $CCACHE -C >/dev/null

    cd dir1
    CCACHE_BASEDIR="$PWD" $CCACHE $COMPILER -I$PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    CCACHE_BASEDIR="$PWD" $CCACHE $COMPILER -I $PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    cd ..

    ##################################################################
    # Setting CCACHE_BASEDIR will result in a cache hit because -I arguments
    # are rewritten, as are the paths stored in the manifest.
    testname="set CCACHE_BASEDIR, direct lookup"
    $CCACHE -z >/dev/null
    $CCACHE -C >/dev/null
    unset CCACHE_NODIRECT

    cd dir1
    CCACHE_BASEDIR="$PWD" $CCACHE $COMPILER -I$PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    CCACHE_BASEDIR="$PWD" $CCACHE $COMPILER -I $PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    CCACHE_NODIRECT=1
    export CCACHE_NODIRECT

    ##################################################################
    # CCACHE_BASEDIR="$PWD" is the default.
    testname="default CCACHE_BASEDIR"
    cd dir1
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -I$PWD/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 0
    cd ..

    ##################################################################
    # Check that rewriting triggered by CCACHE_BASEDIR also affects stderr.
    testname="stderr"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -Wall -W -I$PWD -c $PWD/stderr.c -o $PWD/stderr.o 2>stderr.txt
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    if grep -q $PWD stderr.txt; then
        test_failed "Base dir ($PWD) found in stderr:\n`cat stderr.txt`"
    fi

    $CCACHE $COMPILER -Wall -W -I$PWD -c $PWD/stderr.c -o $PWD/stderr.o 2>stderr.txt
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    if grep -q $PWD stderr.txt; then
        test_failed "Base dir ($PWD) found in stderr:\n`cat stderr.txt`"
    fi
}

######################################################################
# main program

suites="$*"
rm -rf $TESTDIR
mkdir $TESTDIR
cd $TESTDIR || exit 1

CCACHE_DIR=$PWD/.ccache
export CCACHE_DIR
CCACHE_LOGFILE=$PWD/ccache.log
export CCACHE_LOGFILE
CCACHE_NODIRECT=1
export CCACHE_NODIRECT

mkdir $CCACHE_DIR

# ---------------------------------------

all_suites="
base
link
hardlink
cpp2
nlevels4
nlevels1
direct
basedir
"

if [ -z "$suites" ]; then
    suites="$all_suites"
fi

for suite in $suites; do
    run_suite $suite
done

# ---------------------------------------

cd ..
rm -rf $TESTDIR
echo test done - OK
exit 0
