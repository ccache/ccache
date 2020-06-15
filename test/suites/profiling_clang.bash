SUITE_profiling_clang_PROBE() {
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
    elif ! which llvm-profdata$CLANG_VERSION_SUFFIX >/dev/null 2>/dev/null; then
        echo "llvm-profdata$CLANG_VERSION_SUFFIX tool not found"
    fi
}

SUITE_profiling_clang_SETUP() {
    echo 'int main(void) { return 0; }' >test.c
    unset CCACHE_NODIRECT
}

SUITE_profiling_clang() {
    # -------------------------------------------------------------------------
    TEST "-fprofile-use=file"

    mkdir data

    $CCACHE_COMPILE -fprofile-generate=data -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $COMPILER -fprofile-generate=data test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata data/default_*.profraw

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata data/default_*.profraw

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-instr-use"

    mkdir data

    $CCACHE_COMPILE -fprofile-instr-generate -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $COMPILER -fprofile-instr-generate test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output default.profdata default.profraw

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    echo >>default.profdata  # Dummy change to trigger modification

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-instr-use=file"

    $CCACHE_COMPILE -fprofile-instr-generate=foo.profraw -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $COMPILER -fprofile-instr-generate=data=foo.profraw test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata foo.profraw

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    echo >>foo.profdata  # Dummy change to trigger modification

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-sample-use"

    echo 'main:1:1' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -fprofile-sample-accurate -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -fprofile-sample-accurate -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 2

    echo 'main:2:2' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 3

     echo 'main:1:1' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache miss' 3
}
