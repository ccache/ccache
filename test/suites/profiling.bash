SUITE_profiling_PROBE() {
    touch test.c
    if ! $COMPILER -fprofile-generate -c test.c 2>/dev/null; then
        echo "compiler does not support profiling"
    fi
    if $COMPILER_TYPE_CLANG && ! command -v llvm-profdata$CLANG_VERSION_SUFFIX >/dev/null; then
        echo "llvm-profdata$CLANG_VERSION_SUFFIX tool not found"
    fi
}

SUITE_profiling_SETUP() {
    echo 'int main(void) { return 0; }' >test.c
    unset CCACHE_NODIRECT
}

SUITE_profiling() {
    # -------------------------------------------------------------------------
    TEST "-fprofile-use, missing file"

    $CCACHE_COMPILE -fprofile-use -c test.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 0
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "-fbranch-probabilities, missing file"

    $CCACHE_COMPILE -fbranch-probabilities -c test.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 0
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "-fprofile-use=file, missing file"

    $CCACHE_COMPILE -fprofile-use=data.gcda -c test.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 0
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "-fprofile-use"

    $CCACHE_COMPILE -fprofile-generate -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $COMPILER -fprofile-generate test.o -o test

    ./test
    merge_profiling_data .

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    ./test
    merge_profiling_data .

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-use=dir"

    mkdir data

    $CCACHE_COMPILE -fprofile-generate=data -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $COMPILER -fprofile-generate=data test.o -lgcov -o test

    ./test
    merge_profiling_data data

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    ./test
    merge_profiling_data data

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage with -fprofile-dir"

    # GCC 9 and newer creates a mangled .gcno filename (still in the current
    # working directory) if -fprofile-dir is given.

    for flag in "" -fprofile-dir=dir; do
        for dir in . subdir; do
            $CCACHE -z >/dev/null

            mkdir -p "$dir"
            touch "$dir/test.c"
            find -name '*.gcno' -delete

            $REAL_COMPILER $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
            gcno_name=$(find -name '*.gcno')
            rm "$gcno_name"

            $CCACHE_COMPILE $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
            expect_stat 'cache hit (direct)' 0
            expect_stat 'cache miss' 1
            expect_exists "$gcno_name"
            rm "$gcno_name"

            $CCACHE_COMPILE $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
            expect_stat 'cache hit (direct)' 1
            expect_stat 'cache miss' 1
            expect_exists "$gcno_name"
            rm "$gcno_name"
        done
    done
}

merge_profiling_data() {
    local dir=$1
    if $COMPILER_TYPE_CLANG; then
        llvm-profdata$CLANG_VERSION_SUFFIX merge -output $dir/default.profdata $dir/*.profraw
    fi
}
