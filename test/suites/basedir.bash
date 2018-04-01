SUITE_basedir_SETUP() {
    unset CCACHE_NODIRECT

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include "test.h"
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
    CCACHE_BASEDIR="`pwd`" $CCACHE_COMPILE -I`pwd`/include -c src/test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mkdir subdir
    ln -s `pwd`/include subdir/symlink

    # Rewriting triggered by CCACHE_BASEDIR should handle paths with multiple
    # slashes, redundant "/." parts and "foo/.." parts correctly. Note that the
    # ".." part of the path is resolved after the symlink has been resolved.
    CCACHE_BASEDIR=`pwd` $CCACHE_COMPILE -I`pwd`//./subdir/symlink/../include -c `pwd`/src/test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
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
