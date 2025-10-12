SUITE_profiling_clang_PROBE() {
    touch test.c
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
    elif ! command -v llvm-profdata$CLANG_VERSION_SUFFIX >/dev/null; then
        echo "llvm-profdata$CLANG_VERSION_SUFFIX tool not found"
    elif ! $COMPILER -c -fdebug-prefix-map=old=new test.c 2>/dev/null; then
        echo "compiler does not support -fdebug-prefix-map"
    elif ! $COMPILER -c -fprofile-sample-accurate test.c 2>/dev/null; then
        echo "compiler does not support -fprofile-sample-accurate"
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
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -fprofile-generate=data test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata data/default_*.profraw

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata data/default_*.profraw

    $CCACHE_COMPILE -fprofile-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-instr-use"

    mkdir data

    $CCACHE_COMPILE -fprofile-instr-generate -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -fprofile-instr-generate test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output default.profdata default.profraw

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    echo >>default.profdata  # Dummy change to trigger modification

    $CCACHE_COMPILE -fprofile-instr-use -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-instr-use=file"

    $CCACHE_COMPILE -fprofile-instr-generate=foo.profraw -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -fprofile-instr-generate=data=foo.profraw test.o -o test

    ./test
    llvm-profdata$CLANG_VERSION_SUFFIX merge -output foo.profdata foo.profraw

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    echo >>foo.profdata  # Dummy change to trigger modification

    $CCACHE_COMPILE -fprofile-instr-use=foo.profdata -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-sample-use"

    echo 'main:1:1' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -fprofile-sample-accurate -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -fprofile-sample-accurate -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2

    echo 'main:2:2' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 3

    echo 'main:1:1' > sample.prof

    $CCACHE_COMPILE -fprofile-sample-use=sample.prof -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-update=single"

    if $COMPILER -fprofile-update=single -fprofile-generate -c test.c 2>/dev/null; then

        $CCACHE_COMPILE -fprofile-update=single -fprofile-generate -c test.c
        expect_stat direct_cache_hit 0
        expect_stat cache_miss 1

        $COMPILER -fprofile-generate -fprofile-update=single test.o -o test

        ./test
        llvm-profdata$CLANG_VERSION_SUFFIX merge -output default.profdata default_*.profraw

        $CCACHE_COMPILE -fprofile-update=single -fprofile-generate -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 1

        $CCACHE_COMPILE -fprofile-use -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 2
    fi

    # -------------------------------------------------------------------------
    TEST "-fprofile-update=atomic"

    if $COMPILER -fprofile-update=atomic -fprofile-generate -c test.c 2>/dev/null; then

        $CCACHE_COMPILE -fprofile-update=atomic -fprofile-generate -c test.c
        expect_stat direct_cache_hit 0
        expect_stat cache_miss 1

        $COMPILER -fprofile-generate -fprofile-update=atomic test.o -o test

        ./test
        llvm-profdata$CLANG_VERSION_SUFFIX merge -output default.profdata default_*.profraw

        $CCACHE_COMPILE -fprofile-update=atomic -fprofile-generate -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 1

        $CCACHE_COMPILE -fprofile-use -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 2
    fi
}
