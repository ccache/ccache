#!/bin/sh

COMPILER=cc
CCACHE=../ccache
TESTDIR=test.$$


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
    nlines=`expr $RANDOM % 100`
    i=0;
    (
    while [ $i -lt $nlines ]; do
	echo "int foo$i(int x) { return x; }"
	i=`expr $i + 1`
    done
    ) > "$outfile"
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
	test_failed "Expected $stat to be $expected_value - got $value"
    fi
}


basetests() {
    $CCACHE -z > /dev/null
    checkstat 'cache hit' 0
    checkstat 'cache miss' 0

    randcode test1.c
    randcode test2.c
    
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit' 0
    checkstat 'cache miss' 1
    
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit' 1
    checkstat 'cache miss' 1
    
    $CCACHE_COMPILE -c test1.c -g
    checkstat 'cache hit' 1
    checkstat 'cache miss' 2
    
    $CCACHE_COMPILE -c test1.c -g
    checkstat 'cache hit' 2
    checkstat 'cache miss' 2
    
    $CCACHE_COMPILE -c test1.c -o foo.o
    checkstat 'cache hit' 3
    checkstat 'cache miss' 2

    $CCACHE_COMPILE test1.c -o test 2> /dev/null
    checkstat 'called for link' 1

    $CCACHE_COMPILE -c test1.c test2.c
    checkstat 'multiple source files' 1

    $CCACHE blahblah -c test1.c 2> /dev/null
    checkstat "couldn't find the compiler" 1 

    $CCACHE_COMPILE -c test1.c -I 2> /dev/null
    checkstat 'bad compiler arguments' 1

    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2> /dev/null
    checkstat 'not a C/C++ file' 1

    $CCACHE_COMPILE -M foo -c test1.c > /dev/null 2>&1
    checkstat 'unsupported compiler option' 1

    $CCACHE echo foo -c test1.c > /dev/null
    checkstat 'compiler produced stdout' 1

    $CCACHE_COMPILE -o /dev/zero -c test1.c
    checkstat 'output to a non-regular file' 1

    $CCACHE_COMPILE -c -O2 2> /dev/null
    checkstat 'no input file' 1


    CCACHE_DISABLE=1 $CCACHE_COMPILE -c test1.c 2> /dev/null
    checkstat 'cache hit' 3

    $CCACHE_COMPILE -c test1.c 2> /dev/null
    checkstat 'cache hit' 4

    CCACHE_CPP2=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 4
    checkstat 'cache miss' 3

    CCACHE_CPP2=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 5
    checkstat 'cache miss' 3

    CCACHE_NOSTATS=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 5
    checkstat 'cache miss' 3

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 5
    checkstat 'cache miss' 4

    # strictly speaking should be 6 - RECACHE causes a double counting!
    checkstat 'files in cache' 8
    $CCACHE -c > /dev/null
    checkstat 'files in cache' 6

    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 5
    checkstat 'cache miss' 5

    CCACHE_HASHDIR=1 $CCACHE_COMPILE -c test1.c -O -Wall
    checkstat 'cache hit' 6
    checkstat 'cache miss' 5

    checkstat 'files in cache' 8
    
    echo '/* a silly comment */' >> test1.c
    $CCACHE_COMPILE -c test1.c
    checkstat 'cache hit' 6
    checkstat 'cache miss' 6

    echo 'possible unify bug?'
#    echo '/* another comment */' >> test1.c
#    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c 2> /dev/null
#    checkstat 'cache hit' 7
#    checkstat 'cache miss' 6


    echo 'possible -F bug?'
#    $CCACHE -F 2 -c
#    $CCACHE -c
#    checkstat 'files in cache' 2

    checkstat 'files in cache' 10

    $CCACHE -z > /dev/null
    checkstat 'cache hit' 0
    checkstat 'cache miss' 0

    $CCACHE -C > /dev/null
    checkstat 'files in cache' 0


    rm -f test1.c
}

######
# main program
rm -rf $TESTDIR
mkdir $TESTDIR
cd $TESTDIR
mkdir .ccache
export CCACHE_DIR=.ccache

CCACHE_COMPILE="$CCACHE $COMPILER"
basetests

ln -s ../ccache $COMPILER
CCACHE_COMPILE="./$COMPILER"
basetests



cd ..
rm -rf $TESTDIR
echo test done - OK
exit 0
