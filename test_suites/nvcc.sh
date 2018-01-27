nvcc_PROBE() {
    if [ -z "$REAL_NVCC" ]; then
        echo "nvcc is not available"
    elif [ -z "$REAL_CUOBJDUMP" ]; then
        echo "cuobjdump is not available"
    fi
}

nvcc_SETUP() {
    # Test code using only c++ (option --x c++). Faster than compiling cuda.
    cat <<EOF > test_cpp.cu
#ifndef NUM
#define NUM 10000
#endif

void caller() {
  for (int i = 0; i < NUM; ++i);
}
EOF

    # Option files to modify the define.
    cat <<EOF >test1.optf
-DNUM=1
EOF
    cat <<EOF >test2.optf
-DNUM=2
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

nvcc_tests() {
    # Reference file testing was not successful due to different "fatbin" data.
    # Another source of differences are the temporary files created by nvcc;
    # that can be avoided by using the options "--keep --keep-dir ./keep". So
    # instead of comparing the binary object files, we compare the dumps of
    # "cuobjdump -all -elf -symbols -ptx -sass test1.o".
    nvcc_opts_cpp="-Wno-deprecated-gpu-targets -c --x c++"
    nvcc_opts_cuda="-Wno-deprecated-gpu-targets -c"
    nvcc_opts_gpu1="--generate-code arch=compute_50,code=compute_50"
    nvcc_opts_gpu2="--generate-code arch=compute_52,code=sm_52"
    ccache_nvcc_cpp="$CCACHE $REAL_NVCC $nvcc_opts_cpp"
    ccache_nvcc_cuda="$CCACHE $REAL_NVCC $nvcc_opts_cuda"
    cuobjdump="$REAL_CUOBJDUMP -all -elf -symbols -ptx -sass"

    # -------------------------------------------------------------------------
    TEST "Simple mode"

    $REAL_NVCC $nvcc_opts_cpp -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_nvcc_cpp test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Different GPU architectures"

    $REAL_NVCC $nvcc_opts_cuda                 -o reference_test1.o test_cuda.cu
    $REAL_NVCC $nvcc_opts_cuda $nvcc_opts_gpu1 -o reference_test2.o test_cuda.cu
    $REAL_NVCC $nvcc_opts_cuda $nvcc_opts_gpu2 -o reference_test3.o test_cuda.cu
    $cuobjdump reference_test1.o > reference_test1.dump
    $cuobjdump reference_test2.o > reference_test2.dump
    $cuobjdump reference_test3.o > reference_test3.dump
    expect_different_files reference_test1.dump reference_test2.dump
    expect_different_files reference_test1.dump reference_test3.dump
    expect_different_files reference_test2.dump reference_test3.dump

    $ccache_nvcc_cuda test_cuda.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_files reference_test1.dump test1.dump

    # Other GPU.
    $ccache_nvcc_cuda $nvcc_opts_gpu1 test_cuda.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_files reference_test2.dump test1.dump

    $ccache_nvcc_cuda $nvcc_opts_gpu1 test_cuda.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_files reference_test2.dump test1.dump

    # Another GPU.
    $ccache_nvcc_cuda $nvcc_opts_gpu2 test_cuda.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 3
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_files reference_test3.dump test1.dump

    $ccache_nvcc_cuda $nvcc_opts_gpu2 test_cuda.cu
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 3
    expect_stat 'files in cache' 3
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_files reference_test3.dump test1.dump

    # -------------------------------------------------------------------------
    TEST "Different defines"

    $REAL_NVCC $nvcc_opts_cpp            -o reference_test1.o test_cpp.cu
    $REAL_NVCC $nvcc_opts_cpp -DNUM=10   -o reference_test2.o test_cpp.cu
    expect_different_files reference_test1.o reference_test2.o

    $ccache_nvcc_cpp test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # Specified define, but unused. Can only be found by preprocessed mode.
    $ccache_nvcc_cpp -DDUMMYENV=1 test_cpp.cu
    expect_stat "cache hit (preprocessed)" 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # Specified used define.
    $ccache_nvcc_cpp -DNUM=10 test_cpp.cu
    expect_stat "cache hit (preprocessed)" 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_equal_files reference_test2.o test_cpp.o

    $ccache_nvcc_cpp -DNUM=10 test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_equal_files reference_test2.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Option file"

    $REAL_NVCC $nvcc_opts_cpp -optf test1.optf -o reference_test1.o test_cpp.cu
    $REAL_NVCC $nvcc_opts_cpp -optf test2.optf -o reference_test2.o test_cpp.cu
    expect_different_files reference_test1.o reference_test2.o

    $ccache_nvcc_cpp -optf test1.optf test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp -optf test1.optf test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp -optf test2.optf test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_equal_files reference_test2.o test_cpp.o

    $ccache_nvcc_cpp -optf test2.optf test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2
    expect_equal_files reference_test2.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Option --compiler-bindir"

    $REAL_NVCC $nvcc_opts_cpp --compiler-bindir $REAL_COMPILER \
      -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_nvcc_cpp --compiler-bindir $REAL_COMPILER test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp --compiler-bindir $REAL_COMPILER test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Option -ccbin"

    $REAL_NVCC $nvcc_opts_cpp -ccbin $REAL_COMPILER \
      -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_nvcc_cpp -ccbin $REAL_COMPILER test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp -ccbin $REAL_COMPILER test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Option --output-directory"

    $REAL_NVCC $nvcc_opts_cpp --output-directory . \
      -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_nvcc_cpp --output-directory . test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp --output-directory . test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    # -------------------------------------------------------------------------
    TEST "Option -odir"

    $REAL_NVCC $nvcc_opts_cpp -odir . -o reference_test1.o test_cpp.cu

    # First compile.
    $ccache_nvcc_cpp -odir . test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o

    $ccache_nvcc_cpp -odir . test_cpp.cu
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_files reference_test1.o test_cpp.o
}

SUITE_nvcc_PROBE() {
    nvcc_PROBE
}

SUITE_nvcc_SETUP() {
    nvcc_SETUP
}

SUITE_nvcc() {
    nvcc_tests
}
