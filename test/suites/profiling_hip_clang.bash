SUITE_profiling_hip_clang_PROBE() {
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
    elif ! echo | $COMPILER -x hip --cuda-gpu-arch=gfx900 -nogpulib -c - 2> /dev/null; then
        echo "Hip not supported"
    fi
}

SUITE_profiling_hip_clang_SETUP() {
    echo 'int main(void) { return 0; }' >test1.hip
    echo 'int main(void) { int x = 0+0; return 0; }' >test2.hip
    unset CCACHE_NODIRECT
}

SUITE_profiling_hip_clang() {
    # -------------------------------------------------------------------------
    TEST "hip-clang"

    hip_opts="-x hip --cuda-gpu-arch=gfx900 -nogpulib"

    $CCACHE_COMPILE $hip_opts -c test1.hip
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE $hip_opts -c test1.hip
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE $hip_opts --cuda-gpu-arch=gfx906 -c test1.hip
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE $hip_opts -c test2.hip
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE $hip_opts -c test2.hip
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE $hip_opts -Dx=x -c test2.hip
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 3

    $CCACHE_COMPILE $hip_opts -Dx=y -c test2.hip
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 4
}
