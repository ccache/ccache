#!/bin/sh

# a simple test suite for ccache
# tridge@samba.org

if test -n "$CC"; then
    COMPILER="$CC"
else
    COMPILER=cc
fi

CCACHE=../ccache
TESTDIR=testdir.$$

unset CCACHE_DISABLE

test_failed() {
    reason="$1"
    echo $1
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
        test_failed "SUITE: $testsuite, TEST: $testname - Expected $stat to be $expected_value, got $value"
    fi
}

checkfile() {
    if [ ! -f $1 ]; then
        test_failed "SUITE: $testsuite, TEST: $testname - $1 not found"
    fi
    if [ `cat $1` != "$2" ]; then
        test_failed "SUITE: $testsuite, TEST: $testname - Bad content of $2.\nExpected: $2\nActual: `cat $1`"
    fi
}

basetests() {
    echo "starting testsuite $testsuite"
    rm -rf .ccache
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0

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

    testname="BASIC2"
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    testname="debug"
    $CCACHE_COMPILE -c test1.c -g
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2

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

    # strictly speaking should be 6 - RECACHE causes a double counting!
    checkstat 'files in cache' 8
    $CCACHE -c > /dev/null
    checkstat 'files in cache' 6


    testname="CCACHE_HASHDIR"
    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 5
    checkstat 'cache miss' 5

    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -O
    checkstat 'cache hit (preprocessed)' 6
    checkstat 'cache miss' 5

    checkstat 'files in cache' 8

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
    checkstat 'files in cache' 72
    $CCACHE -F 48 -c > /dev/null
    if [ `getstat 'files in cache'` -gt 48 ]; then
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

    testname="zero-stats"
    $CCACHE -z > /dev/null
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0

    testname="clear"
    $CCACHE -C > /dev/null
    checkstat 'files in cache' 0


    rm -f test1.c
}

direct_tests() {
    echo "starting testsuite $testsuite"
    rm -rf .ccache
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

    cat <<EOF >test1.h
/* No more include of test3.h */
int test1;
EOF
    sleep 1 # Sleep to make the include file trusted.
    rm -f test3.h

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Reset CCACHE_NODIRECT again.
    CCACHE_NODIRECT=1
}

######
# main program
rm -rf $TESTDIR
mkdir $TESTDIR
cd $TESTDIR || exit 1
mkdir .ccache
CCACHE_DIR=.ccache
export CCACHE_DIR
CCACHE_NODIRECT=1
export CCACHE_NODIRECT

testsuite="base"
CCACHE_COMPILE="$CCACHE $COMPILER"
basetests

testsuite="link"
ln -s ../ccache $COMPILER
CCACHE_COMPILE="./$COMPILER"
basetests

testsuite="hardlink"
CCACHE_COMPILE="$CCACHE $COMPILER"
CCACHE_HARDLINK=1
export CCACHE_HARDLINK
basetests
unset CCACHE_HARDLINK

testsuite="cpp2"
CCACHE_COMPILE="$CCACHE $COMPILER"
CCACHE_CPP2=1
export CCACHE_CPP2
basetests
unset CCACHE_CPP2

testsuite="nlevels4"
CCACHE_COMPILE="$CCACHE $COMPILER"
CCACHE_NLEVELS=4
export CCACHE_NLEVELS
basetests
unset CCACHE_NLEVELS

testsuite="nlevels1"
CCACHE_COMPILE="$CCACHE $COMPILER"
CCACHE_NLEVELS=1
export CCACHE_NLEVELS
basetests
unset CCACHE_NLEVELS

testsuite="direct"
direct_tests

cd ..
rm -rf $TESTDIR
echo test done - OK
exit 0
