setup_clang() {
    local CUDA_PATH="--cuda-path=/usr/local/cuda"
    if [  ! -z "$CUDA_HOME" ]; then
        local CUDA_PATH="--cuda-path=$CUDA_HOME"
    fi

    export REAL_CLANG="clang $CUDA_PATH"
}

clang_cu_PROBE() {
    if [ -z "$REAL_NVCC" ]; then
        echo "nvcc is not available"
        return
    elif ! command -v cuobjdump >/dev/null; then
        echo "cuobjdump is not available"
        return
    elif ! command -v clang >/dev/null; then
        echo "clang is not available"
        return
    fi

    setup_clang

    touch test.cu
    if ! $REAL_CLANG -c -x cu test.cu  >/dev/null 2>&1; then
        echo "Clang's CUDA support is not compatible."
    fi

}

clang_cu_SETUP() {
    # Test code using only c++ (option -x c++). Faster than compiling cuda.
    cat <<EOF > test_cpp.cu
#ifndef NUM
#define NUM 10000
#endif

void caller() {
  for (int i = 0; i < NUM; ++i);
}
EOF


    # Test code using cuda.
    cat <<EOF >test_cuda.cu
#ifndef NUM
#define NUM 10000
#endif

__global__
void add(int *a, int *b) {
  int i = blockIdx.x;
  if (i < NUM) {
    b[i] = 2 * a[i];
  }
}

void caller() {
  add<<<NUM, 1>>>(NULL,NULL);
}
EOF
}

clang_cu_tests() {
    setup_clang

    clang_opts_cpp="-c -x c++"
    clang_opts_cuda="-c -x cu"
    clang_opts_gpu1="--cuda-gpu-arch=sm_50"
    clang_opts_gpu2="--cuda-gpu-arch=sm_75"
    ccache_clang_cpp="$CCACHE $REAL_CLANG $clang_opts_cpp"
    ccache_clang_cuda="$CCACHE $REAL_CLANG $clang_opts_cuda"
    cuobjdump="cuobjdump -all -elf -symbols -ptx -sass"

    # -------------------------------------------------------------------------
    TEST "Simple mode"

    $REAL_CLANG $clang_opts_cpp -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_clang_cpp test_cpp.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_test1.o test_cpp.o

    $ccache_clang_cpp test_cpp.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_test1.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Different GPU architectures"

    $REAL_CLANG $clang_opts_cuda                 -o reference_test1.o test_cuda.cu
    $REAL_CLANG $clang_opts_cuda $clang_opts_gpu1 -o reference_test2.o test_cuda.cu
    $REAL_CLANG $clang_opts_cuda $clang_opts_gpu2 -o reference_test3.o test_cuda.cu
    $REAL_CLANG $clang_opts_cuda $clang_opts_gpu1  $clang_opts_gpu2 -o reference_test4.o test_cuda.cu

    $cuobjdump reference_test1.o > reference_test1.dump
    $cuobjdump reference_test2.o > reference_test2.dump
    $cuobjdump reference_test3.o > reference_test3.dump
    $cuobjdump reference_test4.o > reference_test4.dump
    expect_different_content reference_test1.dump reference_test2.dump
    expect_different_content reference_test1.dump reference_test3.dump
    expect_different_content reference_test2.dump reference_test3.dump
    expect_different_content reference_test4.dump reference_test3.dump
    expect_different_content reference_test4.dump reference_test2.dump

    $ccache_clang_cuda test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test1.dump test1.dump

    # Other GPU.
    $ccache_clang_cuda $clang_opts_gpu1 test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test2.dump test1.dump

    $ccache_clang_cuda $clang_opts_gpu1 test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test2.dump test1.dump

    # Another GPU.
    $ccache_clang_cuda $clang_opts_gpu2 test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 3
    expect_stat files_in_cache 3
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test3.dump test1.dump

    $ccache_clang_cuda $clang_opts_gpu2 test_cuda.cu
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 3
    expect_stat files_in_cache 3
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test3.dump test1.dump

    # Multi GPU
    $ccache_clang_cuda $clang_opts_gpu1 $clang_opts_gpu2 test_cuda.cu
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 4
    expect_stat files_in_cache 4
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test4.dump test1.dump

    $ccache_clang_cuda $clang_opts_gpu1 $clang_opts_gpu2 test_cuda.cu
    expect_stat preprocessed_cache_hit 3
    expect_stat cache_miss 4
    expect_stat files_in_cache 4
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test4.dump test1.dump

    # -------------------------------------------------------------------------
    TEST "Option -fgpu-rdc"

    $REAL_CLANG $clang_opts_cuda -fgpu-rdc -o reference_test4.o test_cuda.cu
    $cuobjdump reference_test4.o > reference_test4.dump

    $ccache_clang_cuda -fgpu-rdc -o test_cuda.o test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test4.dump
    expect_equal_content test4.dump reference_test4.dump

    $ccache_clang_cuda -fgpu-rdc -o test_cuda.o test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test4.dump
    expect_equal_content test4.dump reference_test4.dump

    # -------------------------------------------------------------------------
    TEST "Different defines"

    $REAL_CLANG $clang_opts_cpp            -o reference_test1.o test_cpp.cu
    $REAL_CLANG $clang_opts_cpp -DNUM=10   -o reference_test2.o test_cpp.cu
    expect_different_content reference_test1.o reference_test2.o

    $ccache_clang_cpp test_cpp.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_test1.o test_cpp.o

    # Specified define, but unused. Can only be found by preprocessed mode.
    $ccache_clang_cpp -DDUMMYENV=1 test_cpp.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_test1.o test_cpp.o

    # Specified used define.
    $ccache_clang_cpp -DNUM=10 test_cpp.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    expect_equal_content reference_test2.o test_cpp.o

    $ccache_clang_cpp -DNUM=10 test_cpp.cu
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    expect_equal_content reference_test2.o test_cpp.o

    TEST "No cache(preprocess failed)"

    $ccache_clang_cuda -DNUM=i test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0

    $ccache_clang_cuda -DNUM=i test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0

    TEST "verbose mode"

    $REAL_CLANG $clang_opts_cuda -o reference_test5.o test_cuda.cu
    $cuobjdump reference_test5.o > reference_test5.dump

    # First compile.
    $ccache_clang_cuda -v test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test5_1.dump
    expect_equal_content test5_1.dump reference_test5.dump

    $ccache_clang_cuda -v test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test5_2.dump
    expect_equal_content test5_2.dump reference_test5.dump
}

SUITE_clang_cu_PROBE() {
    clang_cu_PROBE
}

SUITE_clang_cu_SETUP() {
    clang_cu_SETUP
}

SUITE_clang_cu() {
    clang_cu_tests
}
