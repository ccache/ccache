#!/bin/sh
#
# A simple test suite for ccache.
#
# Copyright (C) 2002-2007 Andrew Tridgell
# Copyright (C) 2009-2012 Joel Rosdahl
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

unset CCACHE_BASEDIR
unset CCACHE_CC
unset CCACHE_COMPILERCHECK
unset CCACHE_COMPRESS
unset CCACHE_CPP2
unset CCACHE_DIR
unset CCACHE_DISABLE
unset CCACHE_EXTENSION
unset CCACHE_EXTRAFILES
unset CCACHE_HARDLINK
unset CCACHE_HASHDIR
unset CCACHE_LOGFILE
unset CCACHE_NLEVELS
unset CCACHE_NODIRECT
unset CCACHE_NOSTATS
unset CCACHE_PATH
unset CCACHE_PREFIX
unset CCACHE_READONLY
unset CCACHE_RECACHE
unset CCACHE_SLOPPINESS
unset CCACHE_TEMPDIR
unset CCACHE_UMASK
unset CCACHE_UNIFY

test_failed() {
    echo "SUITE: \"$testsuite\", TEST: \"$testname\" - $1"
    $CCACHE -s
    cd ..
    echo TEST FAILED
    echo "Test data and log file have been left in $TESTDIR"
    exit 1
}

randcode() {
    outfile="$1"
    nlines=$2
    i=0
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
        test_failed "Expected \"$stat\" to be $expected_value, got $value"
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

checkfilecount() {
    expected=$1
    pattern=$2
    dir=$3
    actual=`find $dir -name "$pattern" | wc -l`
    if [ $actual -ne $expected ]; then
        test_failed "Found $actual (expected $expected) $pattern files in $dir"
    fi
}

sed_in_place() {
    expr=$1
    shift
    for file in $*; do
        sed "$expr" > ${file}.sed < $file
        mv ${file}.sed $file
    done
}

backdate() {
    touch -t 199901010000 "$@"
}

run_suite() {
    rm -rf $CCACHE_DIR
    CCACHE_NODIRECT=1
    export CCACHE_NODIRECT

    echo "starting testsuite $1"
    testsuite=$1

    ${1}_suite

    testname="the tmp directory should be empty"
    if [ -d $CCACHE_DIR/tmp ] && [ "`find $CCACHE_DIR/tmp -type f | wc -l`" -gt 0 ]; then
        test_failed "$CCACHE_DIR/tmp is not empty"
    fi
}

base_tests() {
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

    testname="linkobj"
    $CCACHE_COMPILE foo.o -o test 2> /dev/null
    checkstat 'called for link' 2

    testname="preprocessing"
    $CCACHE_COMPILE -E -c test1.c > /dev/null 2>&1
    checkstat 'called for preprocessing' 1

    testname="multiple"
    $CCACHE_COMPILE -c test1.c test2.c
    checkstat 'multiple source files' 1

    testname="find"
    $CCACHE blahblah -c test1.c 2> /dev/null
    checkstat "couldn't find the compiler" 1

    testname="bad"
    $CCACHE_COMPILE -c test1.c -I 2> /dev/null
    checkstat 'bad compiler arguments' 1

    testname="unsupported source language"
    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2> /dev/null
    checkstat 'unsupported source language' 1

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

    $CCACHE -C >/dev/null

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

    testname="-x c"
    $CCACHE_COMPILE -x c -c test1.ccc
    checkstat 'cache hit (preprocessed)' 10
    checkstat 'cache miss' 39
    $CCACHE_COMPILE -x c -c test1.ccc
    checkstat 'cache hit (preprocessed)' 11
    checkstat 'cache miss' 39

    testname="-xc"
    $CCACHE_COMPILE -xc -c test1.ccc
    checkstat 'cache hit (preprocessed)' 12
    checkstat 'cache miss' 39

    testname="-x none"
    $CCACHE_COMPILE -x assembler -x none -c test1.c
    checkstat 'cache hit (preprocessed)' 13
    checkstat 'cache miss' 39

    testname="-x unknown"
    $CCACHE_COMPILE -x unknown -c test1.c 2>/dev/null
    checkstat 'cache hit (preprocessed)' 13
    checkstat 'cache miss' 39
    checkstat 'unsupported source language' 2

    testname="-D not hashed"
    $CCACHE_COMPILE -DNOT_AFFECTING=1 -c test1.c 2>/dev/null
    checkstat 'cache hit (preprocessed)' 14
    checkstat 'cache miss' 39

    if [ -x /usr/bin/printf ]; then
        /usr/bin/printf '#include <wchar.h>\nwchar_t foo[] = L"\xbf";\n' >latin1.c
        if CCACHE_DISABLE=1 $COMPILER -c -finput-charset=latin1 latin1.c >/dev/null 2>&1; then
            testname="-finput-charset"
            CCACHE_CPP2=1 $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
            checkstat 'cache hit (preprocessed)' 14
            checkstat 'cache miss' 40
            $CCACHE_COMPILE -c -finput-charset=latin1 latin1.c
            checkstat 'cache hit (preprocessed)' 15
            checkstat 'cache miss' 40
        fi
    fi

    testname="compilercheck=mtime"
    $CCACHE -Cz >/dev/null
    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
# A comment
EOF
    chmod +x compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    sed_in_place 's/comment/yoghurt/' compiler.sh # Don't change the size
    chmod +x compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    touch compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2

    testname="compilercheck=content"
    $CCACHE -z >/dev/null
    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    backdate compiler.sh
    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    echo "# Compiler upgrade" >>compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2

    testname="compilercheck=none"
    $CCACHE -z >/dev/null
    backdate compiler.sh
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 1

    testname="compilercheck=command"
    $CCACHE -z >/dev/null
    backdate compiler.sh
    CCACHE_COMPILERCHECK='echo %compiler%' $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK="echo ./compiler.sh" $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1
    cat <<EOF >foobar.sh
#!/bin/sh
echo foo
echo bar
EOF
    chmod +x foobar.sh
    CCACHE_COMPILERCHECK='./foobar.sh' $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2
    CCACHE_COMPILERCHECK='echo foo; echo bar' $CCACHE ./compiler.sh -c test1.c
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 2

    testname="compilercheck=unknown_command"
    $CCACHE -z >/dev/null
    backdate compiler.sh
    CCACHE_COMPILERCHECK="unknown_command" $CCACHE ./compiler.sh -c test1.c 2>/dev/null
    if [ "$?" -eq 0 ]; then
        test_failed "Expected failure running unknown_command to verify compiler but was success"
    fi
    checkstat 'compiler check failed' 1

    testname="no object file"
    cat <<'EOF' >test_no_obj.c
int test_no_obj;
EOF
    cat <<'EOF' >prefix-remove.sh
#!/bin/sh
"$@"
[ x$3 = x-o ] && rm $4
EOF
    chmod +x prefix-remove.sh
    CCACHE_PREFIX=`pwd`/prefix-remove.sh $CCACHE_COMPILE -c test_no_obj.c
    checkstat 'compiler produced no output' 1

    testname="empty object file"
    cat <<'EOF' >test_empty_obj.c
int test_empty_obj;
EOF
    cat <<'EOF' >prefix-empty.sh
#!/bin/sh
"$@"
[ x$3 = x-o ] && cp /dev/null $4
EOF
    chmod +x prefix-empty.sh
    CCACHE_PREFIX=`pwd`/prefix-empty.sh $CCACHE_COMPILE -c test_empty_obj.c
    checkstat 'compiler produced empty output' 1

    testname="stderr-files"
    $CCACHE -Cz >/dev/null
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
    checkstat 'files in cache' 0
    $CCACHE_COMPILE -Wall -W -c stderr.c 2>/dev/null
    num=`find $CCACHE_DIR -name '*.stderr' | wc -l`
    if [ $num -ne 1 ]; then
        test_failed "$num stderr files found, expected 1"
    fi
    checkstat 'files in cache' 2

    testname="zero-stats"
    $CCACHE -z > /dev/null
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0
    checkstat 'files in cache' 2

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
    if [ `dirname $COMPILER` = . ]; then
        ln -s ../ccache $COMPILER
        CCACHE_COMPILE="./$COMPILER"
        base_tests
    else
        echo "Compiler ($COMPILER) not taken from PATH -- not running link test"
    fi
}

hardlink_suite() {
    CCACHE_COMPILE="$CCACHE $COMPILER"
    CCACHE_HARDLINK=1
    export CCACHE_HARDLINK
    CCACHE_NOCOMPRESS=1
    export CCACHE_NOCOMPRESS
    base_tests
    unset CCACHE_HARDLINK
    unset CCACHE_NOCOMPRESS
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
    backdate test1.h test2.h test3.h

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
    # Check that corrupt manifest files are handled and rewritten.
    testname="corrupt manifest file"
    $CCACHE -z >/dev/null
    manifest_file=`find $CCACHE_DIR -name '*.manifest'`
    rm $manifest_file
    touch $manifest_file
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 0
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 1
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
    backdate test3.h
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
    backdate test1.h

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

    rm -f other.d

    ##################################################################
    # Check calculation of dependency file names.
    $CCACHE -Cz >/dev/null
    checkstat 'files in cache' 0
    mkdir test.dir
    for ext in .obj "" . .foo.bar; do
        testname="dependency file calculation from object file 'test$ext'"
        dep_file=test.dir/`echo test$ext | sed 's/\.[^.]*\$//'`.d
        $CCACHE $COMPILER -MD -c test.c -o test.dir/test$ext
        rm -f $dep_file
        $CCACHE $COMPILER -MD -c test.c -o test.dir/test$ext
        if [ ! -f $dep_file ]; then
            test_failed "$dep_file missing"
        fi
    done
    rm -rf test.dir
    checkstat 'files in cache' 12

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
    $CCACHE -C >/dev/null
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
    # Check that -Wp,-MD,file.d,-P disables direct mode.
    testname="-Wp,-MD,file.d,-P"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -Wp,-MD,$DEVNULL,-P test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c -Wp,-MD,$DEVNULL,-P test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    ##################################################################
    # Check that -Wp,-MMD,file.d,-P disables direct mode.
    testname="-Wp,-MDD,file.d,-P"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -c -Wp,-MMD,$DEVNULL,-P test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c -Wp,-MMD,$DEVNULL,-P test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    ##################################################################
    # Test some header modifications to get multiple objects in the manifest.
    testname="several objects"
    $CCACHE -z >/dev/null
    for i in 0 1 2 3 4; do
        echo "int test1_$i;" >>test1.h
        backdate test1.h
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
    $CCACHE -C >/dev/null
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

    CCACHE_NODIRECT=1
    export CCACHE_NODIRECT
    $CCACHE $COMPILER -Wall -W -c cpp-warning.c 2>stderr-cpp.txt
    unset CCACHE_NODIRECT
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
    # Check that changes in comments are ignored when hashing.
    testname="changes in comments"
    $CCACHE -C >/dev/null
    $CCACHE -z >/dev/null
    cat <<EOF >comments.h
/*
 * /* foo comment
 */
EOF
    backdate comments.h
    cat <<'EOF' >comments.c
#include "comments.h"
char test[] = "\
/* apple */ // banana"; // foo comment
EOF

    $CCACHE $COMPILER -c comments.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    sed_in_place 's/foo/ignored/' comments.h comments.c
    backdate comments.h

    $CCACHE $COMPILER -c comments.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    # Check that comment-like string contents are hashed.
    sed_in_place 's/apple/orange/' comments.c
    backdate comments.h

    $CCACHE $COMPILER -c comments.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2

    ##################################################################
    # Check that it's possible to compile and cache an empty source code file.
    testname="empty source file"
    $CCACHE -Cz >/dev/null
    cp /dev/null empty.c
    $CCACHE $COMPILER -c empty.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c empty.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Check that empty include files are handled as well.
    testname="empty include file"
    $CCACHE -Cz >/dev/null
    cp /dev/null empty.h
    cat <<EOF >include_empty.c
#include "empty.h"
EOF
    backdate empty.h
    $CCACHE $COMPILER -c include_empty.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c include_empty.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Check that direct mode correctly detects file name/path changes.
    testname="__FILE__ in source file"
    $CCACHE -Cz >/dev/null
    cat <<EOF >file.c
#define file __FILE__
int test;
EOF
    $CCACHE $COMPILER -c file.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c file.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c `pwd`/file.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2

    testname="__FILE__ in include file"
    $CCACHE -Cz >/dev/null
    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF
    $CCACHE $COMPILER -c file_h.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c file_h.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    mv file_h.c file2_h.c
    $CCACHE $COMPILER -c `pwd`/file2_h.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2

    ##################################################################
    # Check that direct mode ignores __FILE__ if sloppiness is specified.
    testname="__FILE__ in source file, sloppy"
    $CCACHE -Cz >/dev/null
    cat <<EOF >file.c
#define file __FILE__
int test;
EOF
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c file.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c file.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c `pwd`/file.c
    checkstat 'cache hit (direct)' 2
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    testname="__FILE__ in include file, sloppy"
    $CCACHE -Cz >/dev/null
    cat <<EOF >file.h
#define file __FILE__
int test;
EOF
    backdate file.h
    cat <<EOF >file_h.c
#include "file.h"
EOF
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c file_h.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c file_h.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    mv file_h.c file2_h.c
    CCACHE_SLOPPINESS=file_macro $CCACHE $COMPILER -c `pwd`/file2_h.c
    checkstat 'cache hit (direct)' 2
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Check that we never get direct hits when __TIME__ is used.
    testname="__TIME__ in source file"
    $CCACHE -Cz >/dev/null
    cat <<EOF >time.c
#define time __TIME__
int test;
EOF
    $CCACHE $COMPILER -c time.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c time.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    testname="__TIME__ in include time"
    $CCACHE -Cz >/dev/null
    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h
    cat <<EOF >time_h.c
#include "time.h"
EOF
    $CCACHE $COMPILER -c time_h.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c time_h.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    ##################################################################
    # Check that direct mode ignores __TIME__ when sloppiness is specified.
    testname="__TIME__ in source file, sloppy"
    $CCACHE -Cz >/dev/null
    cat <<EOF >time.c
#define time __TIME__
int test;
EOF
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c time.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c time.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    testname="__TIME__ in include time, sloppy"
    $CCACHE -Cz >/dev/null
    cat <<EOF >time.h
#define time __TIME__
int test;
EOF
    backdate time.h
    cat <<EOF >time_h.c
#include "time.h"
EOF
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c time_h.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c time_h.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Check that a too new include file turns off direct mode.
    testname="too new include file"
    $CCACHE -Cz >/dev/null
    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h
    $CCACHE $COMPILER -c new.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER -c new.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    ##################################################################
    # Check that include file mtime is ignored when sloppiness is specified.
    testname="too new include file, sloppy"
    $CCACHE -Cz >/dev/null
    cat <<EOF >new.c
#include "new.h"
EOF
    cat <<EOF >new.h
int test;
EOF
    touch -t 203801010000 new.h
    CCACHE_SLOPPINESS=include_file_mtime $CCACHE $COMPILER -c new.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=include_file_mtime $CCACHE $COMPILER -c new.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    ##################################################################
    # Check that environment variables that affect the preprocessor are taken
    # into account.
    testname="environment variables"
    $CCACHE -Cz >/dev/null
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
    CPATH=subdir1 $CCACHE $COMPILER -c foo.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CPATH=subdir1 $CCACHE $COMPILER -c foo.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CPATH=subdir2 $CCACHE $COMPILER -c foo.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2 # subdir2 is part of the preprocessor output
    CPATH=subdir2 $CCACHE $COMPILER -c foo.c
    checkstat 'cache hit (direct)' 2
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2
}

basedir_suite() {
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
    backdate dir1/include/test.h dir2/include/test.h

    cat <<EOF >stderr.h
int stderr(void)
{
	/* Trigger warning by having no return statement. */
}
EOF
    cat <<EOF >stderr.c
#include <stderr.h>
EOF
    backdate stderr.h

    ##################################################################
    # CCACHE_BASEDIR="" and using absolute include path will result in a cache
    # miss.
    testname="empty CCACHE_BASEDIR"
    $CCACHE -z >/dev/null

    cd dir1
    CCACHE_BASEDIR="" $CCACHE $COMPILER -I`pwd`/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    CCACHE_BASEDIR="" $CCACHE $COMPILER -I`pwd`/include -c src/test.c
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
    CCACHE_BASEDIR="`pwd`" $CCACHE $COMPILER -I`pwd`/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    # The space after -I is there to test an extra code path.
    CCACHE_BASEDIR="`pwd`" $CCACHE $COMPILER -I `pwd`/include -c src/test.c
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
    CCACHE_BASEDIR="`pwd`" $CCACHE $COMPILER -I`pwd`/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    cd dir2
    CCACHE_BASEDIR="`pwd`" $CCACHE $COMPILER -I`pwd`/include -c src/test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    ##################################################################
    # CCACHE_BASEDIR="" is the default.
    testname="default CCACHE_BASEDIR"
    cd dir1
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER -I`pwd`/include -c src/test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    cd ..

    ##################################################################
    # Rewriting triggered by CCACHE_BASEDIR should handle paths with multiple
    # slashes correctly.
    testname="path normalization"
    cd dir1
    $CCACHE -z >/dev/null
    CCACHE_BASEDIR=`pwd` $CCACHE $COMPILER -I`pwd`//include -c `pwd`//src/test.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0
    cd ..

    ##################################################################
    # Check that rewriting triggered by CCACHE_BASEDIR also affects stderr.
    testname="stderr"
    $CCACHE -z >/dev/null
    CCACHE_BASEDIR=`pwd` $CCACHE $COMPILER -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi

    CCACHE_BASEDIR=`pwd` $CCACHE $COMPILER -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi
}

compression_suite() {
    ##################################################################
    # Create some code to compile.
    cat <<EOF >test.c
int test;
EOF

    ##################################################################
    # Check that compressed and uncompressed files get the same hash sum.
    testname="compression hash sum"
    CCACHE_COMPRESS=1 $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    CCACHE_COMPRESS=1 $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 1
}

readonly_suite() {
    ##################################################################
    # Create some code to compile.
    echo "int test;" >test.c
    echo "int test2;" >test2.c

    # Cache a compilation.
    $CCACHE $COMPILER -c test.c -o test.o
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    # Make the cache readonly
    # Check that readonly mode finds the result.
    testname="cache hit"
    rm -f test.o
    chmod -R a-w $CCACHE_DIR
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp CCACHE_PREFIX=false $CCACHE $COMPILER -c test.c -o test.o
    status=$?
    chmod -R a+w $CCACHE_DIR
    if [ $status -ne 0 ]; then
        test_failed "failure when compiling test.c readonly"
    fi
    if [ ! -f test.o ]; then
        test_failed "test.o missing"
    fi

    # Check that readonly mode doesn't try to store new results.
    testname="cache miss"
    files_before=`find $CCACHE_DIR -type f | wc -l`
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp $CCACHE $COMPILER -c test2.c -o test2.o
    if [ $? -ne 0 ]; then
        test_failed "failure when compiling test2.c readonly"
    fi
    if [ ! -f test2.o ]; then
        test_failed "test2.o missing"
    fi
    files_after=`find $CCACHE_DIR -type f | wc -l`
    if [ $files_before -ne $files_after ]; then
        test_failed "readonly mode stored files in the cache"
    fi

    # Check that readonly mode and direct mode works.
    unset CCACHE_NODIRECT
    files_before=`find $CCACHE_DIR -type f | wc -l`
    CCACHE_READONLY=1 CCACHE_TEMPDIR=/tmp $CCACHE $COMPILER -c test.c -o test.o
    CCACHE_NODIRECT=1
    export CCACHE_NODIRECT
    if [ $? -ne 0 ]; then
        test_failed "failure when compiling test2.c readonly"
    fi
    files_after=`find $CCACHE_DIR -type f | wc -l`
    if [ $files_before -ne $files_after ]; then
        test_failed "readonly mode + direct mode stored files in the cache"
    fi

    ##################################################################
}

extrafiles_suite() {
    ##################################################################
    # Create some code to compile.
    cat <<EOF >test.c
int test;
EOF
    echo a >a
    echo b >b

    ##################################################################
    # Test the CCACHE_EXTRAFILES feature.

    testname="cache hit"
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    testname="cache miss"
    $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    testname="cache miss a b"
    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2

    testname="cache hit a b"
    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 2

    testname="cache miss a b2"
    echo b2 >b
    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 3

    testname="cache hit a b2"
    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 3
    checkstat 'cache miss' 3

    testname="cache miss doesntexist"
    CCACHE_EXTRAFILES="doesntexist" $CCACHE $COMPILER -c test.c
    checkstat 'cache hit (preprocessed)' 3
    checkstat 'cache miss' 3
    checkstat 'error hashing extra file' 1
}

prepare_cleanup_test() {
    dir=$1
    rm -rf $dir
    mkdir -p $dir
    i=0
    while [ $i -lt 10 ]; do
        perl -e 'print "A" x 4017' >$dir/result$i-4017.o
        touch $dir/result$i-4017.stderr
        touch $dir/result$i-4017.d
        if [ $i -gt 5 ]; then
            backdate $dir/result$i-4017.stderr
        fi
        i=`expr $i + 1`
    done
    # NUMFILES: 30, TOTALSIZE: 40 KiB, MAXFILES: 0, MAXSIZE: 0
    echo "0 0 0 0 0 0 0 0 0 0 0 30 40 0 0" >$dir/stats
}

cleanup_suite() {
    testname="clear"
    prepare_cleanup_test $CCACHE_DIR/a
    $CCACHE -C >/dev/null
    checkfilecount 0 '*.o' $CCACHE_DIR
    checkfilecount 0 '*.d' $CCACHE_DIR
    checkfilecount 0 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 0

    testname="forced cleanup, no limits"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    checkfilecount 10 '*.o' $CCACHE_DIR
    checkfilecount 10 '*.d' $CCACHE_DIR
    checkfilecount 10 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 30

    testname="forced cleanup, file limit"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    # floor(0.8 * 9) = 7
    checkfilecount 7 '*.o' $CCACHE_DIR
    checkfilecount 7 '*.d' $CCACHE_DIR
    checkfilecount 7 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 21
    for i in 0 1 2 3 4 5 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 6 7 8; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    testname="forced cleanup, size limit"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    # (4/10) * 10 * 4 * 16 = 256
    $CCACHE -F 0 -M 256K >/dev/null
    $CCACHE -c >/dev/null
    # floor(0.8 * 4) = 3
    checkfilecount 3 '*.o' $CCACHE_DIR
    checkfilecount 3 '*.d' $CCACHE_DIR
    checkfilecount 3 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 9
    for i in 3 4 5; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 0 1 2 6 7 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    testname="autocleanup"
    $CCACHE -C >/dev/null
    for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
        prepare_cleanup_test $CCACHE_DIR/$x
    done
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    touch empty.c
    checkfilecount 160 '*.o' $CCACHE_DIR
    checkfilecount 160 '*.d' $CCACHE_DIR
    checkfilecount 160 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 480
    $CCACHE $COMPILER -c empty.c -o empty.o
    # floor(0.8 * 9) = 7
    checkfilecount 157 '*.o' $CCACHE_DIR
    checkfilecount 156 '*.d' $CCACHE_DIR
    checkfilecount 156 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 469

    testname="sibling cleanup"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    backdate $CCACHE_DIR/a/result2-4017.stderr
    $CCACHE -c >/dev/null
    # floor(0.8 * 9) = 7
    checkfilecount 7 '*.o' $CCACHE_DIR
    checkfilecount 7 '*.d' $CCACHE_DIR
    checkfilecount 7 '*.stderr' $CCACHE_DIR
    checkstat 'files in cache' 21
    for i in 0 1 3 4 5 8 9; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ ! -f $file ]; then
            test_failed "File $file removed when it shouldn't"
        fi
    done
    for i in 2 6 7; do
        file=$CCACHE_DIR/a/result$i-4017.o
        if [ -f $file ]; then
            test_failed "File $file not removed when it should"
        fi
    done

    testname="new unknown file"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    touch $CCACHE_DIR/a/abcd.unknown
    $CCACHE -c >/dev/null # update counters
    checkstat 'files in cache' 31
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    if [ ! -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown removed"
    fi
    checkstat 'files in cache' 19

    testname="old unknown file"
    $CCACHE -C >/dev/null
    prepare_cleanup_test $CCACHE_DIR/a
    # (9/10) * 30 * 16 = 432
    $CCACHE -F 432 -M 0 >/dev/null
    touch $CCACHE_DIR/a/abcd.unknown
    backdate $CCACHE_DIR/a/abcd.unknown
    $CCACHE -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.unknown ]; then
        test_failed "$CCACHE_DIR/a/abcd.unknown not removed"
    fi

    testname="cleanup of tmp files"
    $CCACHE -C >/dev/null
    touch $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null # update counters
    checkstat 'files in cache' 1
    backdate $CCACHE_DIR/a/abcd.tmp.efgh
    $CCACHE -c >/dev/null
    if [ -f $CCACHE_DIR/a/abcd.tmp.efgh ]; then
        test_failed "$CCACHE_DIR/a/abcd.tmp.unknown not removed"
    fi
    checkstat 'files in cache' 0

    testname="ignore .nfs* files"
    prepare_cleanup_test $CCACHE_DIR/a
    touch $CCACHE_DIR/a/.nfs0123456789
    $CCACHE -F 0 -M 0 >/dev/null
    $CCACHE -c >/dev/null
    checkfilecount 1 '.nfs*' $CCACHE_DIR
    checkstat 'files in cache' 30
}

pch_suite() {
    unset CCACHE_NODIRECT

    cat <<EOF >pch.c
#include "pch.h"
int main()
{
  void *p = NULL;
  return 0;
}
EOF
    cat <<EOF >pch.h
#include <stdlib.h>
EOF
    cat <<EOF >pch2.c
int main()
{
  void *p = NULL;
  return 0;
}
EOF

    if $COMPILER -fpch-preprocess pch.h 2>/dev/null && [ -f pch.h.gch ] && $COMPILER pch.c -o pch; then
        :
    else
        echo "Compiler (`$COMPILER --version | head -1`) doesn't support precompiled headers -- not running pch test"
        return
    fi

    ##################################################################
    # Tests for creating a .gch.

    backdate pch.h

    testname="create .gch, -c, no -o"
    $CCACHE -zC >/dev/null
    $CCACHE $COMPILER -c pch.h
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    rm -f pch.h.gch
    $CCACHE $COMPILER -c pch.h
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    if [ ! -f pch.h.gch ]; then
        test_failed "pch.h.gch missing"
    fi

    testname="create .gch, no -c, -o"
    $CCACHE -z >/dev/null
    $CCACHE $COMPILER pch.h -o pch.gch
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    $CCACHE $COMPILER pch.h -o pch.gch
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    if [ ! -f pch.gch ]; then
        test_failed "pch.gch missing"
    fi

    ##################################################################
    # Tests for using a .gch.

    rm -f pch.h
    backdate pch.h.gch

    testname="no -fpch-preprocess, #include"
    $CCACHE -Cz >/dev/null
    $CCACHE $COMPILER -c pch.c 2>/dev/null
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0
    # Preprocessor error because GCC can't find the real include file when
    # trying to preprocess:
    checkstat 'preprocessor error' 1

    testname="no -fpch-preprocess, -include, no sloppy time macros"
    $CCACHE -Cz >/dev/null
    $CCACHE $COMPILER -c -include pch.h pch2.c 2>/dev/null
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 0
    # Must enable sloppy time macros:
    checkstat "can't use precompiled header" 1

    testname="no -fpch-preprocess, -include"
    $CCACHE -Cz >/dev/null
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -include pch.h pch2.c 2>/dev/null
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -include pch.h pch2.c 2>/dev/null
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    testname="-fpch-preprocess, #include, no sloppy time macros"
    $CCACHE -Cz >/dev/null
    $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    # Must enable sloppy time macros:
    checkstat "can't use precompiled header" 1

    testname="-fpch-preprocess, #include, sloppy time macros"
    $CCACHE -Cz >/dev/null
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1

    testname="-fpch-preprocess, #include, file changed"
    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch
    CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 1
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 2

    testname="preprocessor mode"
    $CCACHE -Cz >/dev/null
    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 0
    checkstat 'cache miss' 1
    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 1

    testname="preprocessor mode, file changed"
    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch
    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 1
    checkstat 'cache miss' 2
    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS=time_macros $CCACHE $COMPILER -c -fpch-preprocess pch.c
    checkstat 'cache hit (direct)' 0
    checkstat 'cache hit (preprocessed)' 2
    checkstat 'cache miss' 2
}

######################################################################
# main program

suites="$*"
if [ -n "$CC" ]; then
    COMPILER="$CC"
else
    COMPILER=gcc
fi
if [ -z "$CCACHE" ]; then
    CCACHE=`pwd`/ccache
fi

compiler_version="`$COMPILER --version 2>&1 | head -1`"
case $compiler_version in
    *gcc*|2.95*)
        ;;
    *)
        echo "WARNING: Compiler $COMPILER not supported (version: $compiler_version) -- not running tests" >&2
        exit 0
        ;;
esac

TESTDIR=testdir.$$
rm -rf $TESTDIR
mkdir $TESTDIR
cd $TESTDIR || exit 1

CCACHE_DIR=`pwd`/.ccache
export CCACHE_DIR
CCACHE_LOGFILE=`pwd`/ccache.log
export CCACHE_LOGFILE

# ---------------------------------------

all_suites="
base
link          !win32
hardlink
cpp2
nlevels4
nlevels1
basedir       !win32
direct
compression
readonly
extrafiles
cleanup
pch
"

host_os="`uname -s`"
case $host_os in
    *MINGW*|*mingw*)
        export CCACHE_DETECT_SHEBANG
        CCACHE_DETECT_SHEBANG=1
        DEVNULL=NUL
        PATH_DELIM=";"
        all_suites="`echo "$all_suites" | grep -v '!win32'`"
        ;;
    *)
        DEVNULL=/dev/null
        PATH_DELIM=":"
        all_suites="`echo "$all_suites" | cut -d' ' -f1`"
        ;;
esac

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
