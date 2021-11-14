SUITE_nvcc_ldir_PROBE() {
    if [ -z "$REAL_NVCC" ]; then
        echo "nvcc is not available"
        return
    elif ! command -v cuobjdump >/dev/null; then
        echo "cuobjdump is not available"
        return
    fi

    nvcc_dir=$(dirname $REAL_NVCC)
    nvcc_ldir=$nvcc_dir/../nvvm/libdevice
    cicc_path=$nvcc_dir/../nvvm/bin
    nvcc_idir=$nvcc_dir/../include
    # Workaround for Canonical's Ubuntu package.
    [ ! -d $nvcc_ldir ] && nvcc_ldir=/usr/lib/nvidia-cuda-toolkit/libdevice
    [ ! -d $cicc_path ] && cicc_path=/usr/lib/nvidia-cuda-toolkit/bin
    [ ! -d $nvcc_idir ] && nvcc_idir=/usr/include
    if [ ! -d $nvcc_ldir ]; then
        echo "libdevice directory $nvcc_ldir not found"
    elif [ ! -d $cicc_path ]; then
        echo "path $cicc_path not found"
    elif [ ! -d $nvcc_idir ]; then
        echo "include directory $nvcc_idir not found"
    fi

    echo "int main() { return 0; }" | $REAL_NVCC -Wno-deprecated-gpu-targets -ccbin $COMPILER_BIN -c -x cu - 
    if [ $? -ne 0 ]; then
        echo "nvcc of a canary failed; Is CUDA compatible with the host compiler ($COMPILER_BIN)?"
    fi
}

SUITE_nvcc_ldir_SETUP() {
    nvcc_SETUP
}

SUITE_nvcc_ldir() {
    nvcc_opts_cuda="-Wno-deprecated-gpu-targets -c -ccbin $COMPILER_BIN"
    ccache_nvcc_cuda="$CCACHE $REAL_NVCC $nvcc_opts_cuda"
    cuobjdump="cuobjdump -all -elf -symbols -ptx -sass"
    nvcc_dir=$(dirname $REAL_NVCC)
    nvcc_ldir=$nvcc_dir/../nvvm/libdevice
    cicc_path=$nvcc_dir/../nvvm/bin
    nvcc_idir=$nvcc_dir/../include
    # Workaround for Canonical's Ubuntu package.
    [ ! -d $nvcc_ldir ] && nvcc_ldir=/usr/lib/nvidia-cuda-toolkit/libdevice
    [ ! -d $cicc_path ] && cicc_path=/usr/lib/nvidia-cuda-toolkit/bin
    [ ! -d $nvcc_idir ] && nvcc_idir=/usr/include

    # ---------------------------------------------------------------------
    TEST "Option --libdevice-directory"

    OLD_PATH=$PATH
    TEST_OPTS="--libdevice-directory $nvcc_ldir -I $nvcc_idir --dont-use-profile"
    export PATH=$PATH:$cicc_path

    $REAL_NVCC $nvcc_opts_cuda $TEST_OPTS -o reference_test1.o test_cuda.cu
    $cuobjdump reference_test1.o > reference_test1.dump

    # First compile.
    $ccache_nvcc_cuda $TEST_OPTS test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test1.dump test1.dump

    $ccache_nvcc_cuda $TEST_OPTS test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test1.dump test1.dump

    # ---------------------------------------------------------------------
    TEST "Option -ldir"

    TEST_OPTS="-ldir $nvcc_ldir -I $nvcc_idir --dont-use-profile"
    $REAL_NVCC $nvcc_opts_cuda $TEST_OPTS -o reference_test1.o test_cuda.cu
    $cuobjdump reference_test1.o > reference_test1.dump

    # First compile.
    $ccache_nvcc_cuda $TEST_OPTS test_cuda.cu
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test1.dump test1.dump

    $ccache_nvcc_cuda $TEST_OPTS test_cuda.cu
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    $cuobjdump test_cuda.o > test1.dump
    expect_equal_content reference_test1.dump test1.dump

    export PATH=$OLD_PATH
}
