SUITE_masquerading_PROBE() {
    local compiler_binary
    compiler_binary=$(echo $COMPILER | cut -d' ' -f1)
    if [ "$(dirname $compiler_binary)" != . ]; then
        echo "compiler ($compiler_binary) not taken from PATH"
    fi
    if $HOST_OS_WINDOWS || $HOST_OS_CYGWIN; then
        echo "symlinks not supported on $(uname -s)"
        return
    fi
}

SUITE_masquerading_SETUP() {
    local compiler_binary
    compiler_binary=$(echo $COMPILER | cut -d' ' -f1)
    local compiler_args
    compiler_args=$(echo $COMPILER | cut -s -d' ' -f2-)

    ln -s "$CCACHE" $compiler_binary
    CCACHE_COMPILE="./$compiler_binary $compiler_args"
    generate_code 1 test1.c
}

SUITE_masquerading() {
    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink"

    $REAL_COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o
}
