SUITE_basedir_SETUP() {
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

SUITE_basedir() {
    # -------------------------------------------------------------------------
    TEST "Enabled CCACHE_BASEDIR"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    cd ../dir2
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Disabled (default) CCACHE_BASEDIR"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # CCACHE_BASEDIR="" is the default:
    $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    TEST "Path normalization"

    cd dir1
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I$(pwd)/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mkdir subdir

    # Rewriting triggered by CCACHE_BASEDIR should handle paths with multiple
    # slashes, redundant "/." parts and "foo/.." parts correctly.
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)//./subdir/../include -c $(pwd)/src/test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
fi

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    TEST "Symlink to source directory"

    mkdir dir
    cd dir
    mkdir -p d1/d2
    echo '#define A "OK"' >d1/h.h
    cat <<EOF >d1/d2/c.c
#include <stdio.h>
#include "../h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "BUG"' >h.h
    ln -s d1/d2 d3

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/d3/c.c
    $REAL_COMPILER c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi
fi

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    TEST "Symlink to source file"

    mkdir dir
    cd dir
    mkdir d
    echo '#define A "BUG"' >d/h.h
    cat <<EOF >d/c.c
#include <stdio.h>
#include "h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "OK"' >h.h
    ln -s d/c.c c.c

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/c.c
    $REAL_COMPILER c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi
fi

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    TEST "Symlinked build dir inside source dir"

    mkdir build1
    ln -s $(pwd)/build1 dir1/src/build

    mkdir build2
    ln -s $(pwd)/build2 dir2/src/build

    # The file structure now looks like this:
    #
    # build1
    # dir1/include/test.h
    # dir1/src/test.c
    # dir1/src/build -> /absolute/path/to/build1
    #
    # build2
    # dir2/include/test.h
    # dir2/src/test.c
    # dir2/src/build -> /absolute/path/to/build2

    cd dir1/src
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd)/../include -c $(pwd)/test.c -o $(pwd)/build/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    cd ../../dir2/src
    # Apparent CWD:
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd)/../include -c $(pwd)/test.c -o $(pwd)/build/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    # Actual CWD (e.g. from $(CURDIR) in a Makefile):
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd -P)/../include -c $(pwd -P)/test.c -o $(pwd -P)/build/test.o
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
fi

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    TEST "Symlinked source dir inside build dir"

    mkdir build1
    ln -s $(pwd)/dir1 build1/src

    mkdir build2
    ln -s $(pwd)/dir2 build2/src

    # The file structure now looks like this:
    #
    # build1
    # build1/src -> /absolute/path/to/dir1
    # dir1/include/test.h
    # dir1/src/test.c
    #
    # build2
    # build2/src -> /absolute/path/to/dir2
    # dir2/include/test.h
    # dir2/src/test.c

    cd build1
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd)/src/include -c $(pwd)/src/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    cd ../build2
    # Apparent CWD:
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd)/src/include -c $(pwd)/src/src/test.c -o $(pwd)/test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    # Actual CWD:
    CCACHE_BASEDIR=/ $CCACHE_COMPILE -I$(pwd -P)/src/include -c $(pwd -P)/src/src/test.c -o $(pwd -P)/test.o
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
fi

    # -------------------------------------------------------------------------
    TEST "Rewriting in stderr"

    cat <<EOF >stderr.h
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF
    backdate stderr.h
    cat <<EOF >stderr.c
#include <stderr.h>
EOF

    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi

    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -Wall -W -I`pwd` -c `pwd`/stderr.c -o `pwd`/stderr.o 2>stderr.txt
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if grep `pwd` stderr.txt >/dev/null 2>&1; then
        test_failed "Base dir (`pwd`) found in stderr:\n`cat stderr.txt`"
    fi

    # -------------------------------------------------------------------------
    TEST "-MF/-MQ/-MT with absolute paths"

    for option in MF "MF " MQ "MQ " MT "MT "; do
        clear_cache
        cd dir1
        CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..

        cd dir2
        CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..
    done

    # -------------------------------------------------------------------------
    # When BASEDIR is set to /, check that -MF, -MQ and -MT arguments with
    # absolute paths are rewritten to relative and that the dependency file
    # only contains relative paths.
    TEST "-MF/-MQ/-MT with absolute paths and BASEDIR set to /"

    for option in MF "MF " MQ "MQ " MT "MT "; do
        clear_cache
        cd dir1
        CCACHE_BASEDIR="/" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        # Check that there is no absolute path in the dependency file:
        while read line; do
            for file in $line; do
                case $file in /*)
                    test_failed "Absolute file path '$file' found in dependency file '`pwd`/test.d'"
                esac
            done
        done <test.d
        cd ..

        cd dir2
        CCACHE_BASEDIR="/" $CCACHE_COMPILE -I`pwd`/include -MD -${option}`pwd`/test.d -c src/test.c
        expect_stat 'cache hit (direct)' 1
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 1
        cd ..
    done
}
