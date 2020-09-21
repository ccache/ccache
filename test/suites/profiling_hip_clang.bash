SUITE_profiling_clang_PROBE() {
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
    elif ! $COMPILER -x hip --cuda-arch=gfx900 -c - > /dev/null; then
        echo "Hip not supported"
    fi
}

SUITE_profiling_clang_SETUP() {
    echo 'int main(void) { return 0; }' >test.c
    unset CCACHE_NODIRECT
}

SUITE_profiling_clang() {
    # -------------------------------------------------------------------------
    TEST "hip-clang"

    $CCACHE_COMPILE -x hip --cuda-arch=gfx900 -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x hip --cuda-arch=gfx900 -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
}
