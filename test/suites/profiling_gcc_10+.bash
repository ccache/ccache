gcda_cycle() {
    export CCACHE_BASEDIR="$(pwd)"
    $CCACHE_COMPILE --coverage "$@" -c test.c -o test.o
    $COMPILER --coverage -o test test.o
    chmod a+x ./test
    ./test
}

SUITE_profiling_gcc_10+_PROBE() {
    echo 'int main(void) { return 0; }' >test.c
    if ! $COMPILER_TYPE_GCC; then
        echo "compiler is not GCC"
    fi
    if ! $RUN_WIN_XFAIL; then
        echo "this suite does not work on Windows"
    fi
    if ! $COMPILER --coverage -fprofile-prefix-path=. -c test.c 2>/dev/null; then
        echo "compiler does not support -fprofile-prefix-path=path"
    fi
    if ! $COMPILER --coverage -fprofile-dir=. -c test.c 2>/dev/null; then
        echo "compiler does not support -fprofile-dir=path"
    fi
}

SUITE_profiling_gcc_10+_SETUP() {
    echo 'int main(void) { return 0; }' >test.c
    unset CCACHE_NODIRECT
}

SUITE_profiling_gcc_10+() {
    # -------------------------------------------------------------------------
    TEST "-fprofile-prefix-path=path, gcc coverage build"
    # When using a relative path for -fprofile-dir in GCC 9+, absolute object
    # file path will be mangled in the .gcda filename.
    CCACHE_SLOPPINESS_OLD="$CCACHE_SLOPPINESS"
    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS gcno_cwd"

    for dir in obj1 obj2; do
      mkdir "$dir"
      cp test.c "$dir/test.c"
    done

    cd obj1
    gcda_cycle -fprofile-dir=.
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    cd ../obj2
    gcda_cycle -fprofile-dir=.
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS_OLD"

    # -------------------------------------------------------------------------
    TEST "-fprofile-prefix-path=pwd, -fprofile-dir=., gcc coverage build"
    # GCC 10 and newer allows to lstrip the mangled absolute path in the
    # generated gcda file name. This doesn't effect the absolute cwd path in the
    # gcno file (but there's sloppiness for that).
    CCACHE_SLOPPINESS_OLD="$CCACHE_SLOPPINESS"
    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS gcno_cwd"

    for dir in obj1 obj2; do
      mkdir "$dir"
      cp test.c "$dir/test.c"
    done

    cd obj1
    gcda_cycle -fprofile-prefix-path=$(pwd) -fprofile-dir=.
    expect_exists test.gcda
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    cd ../obj2
    gcda_cycle -fprofile-prefix-path=$(pwd) -fprofile-dir=.
    expect_exists test.gcda
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS_OLD"

    # -------------------------------------------------------------------------
    TEST "-fprofile-prefix-path=dummy, -fprofile-dir=., gcc coverage build"
    # lstripping the mangled .gcda filename only works with a correct
    # -fprofile-prefix-path
    CCACHE_SLOPPINESS_OLD="$CCACHE_SLOPPINESS"
    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS gcno_cwd"

    for dir in obj1 obj2; do
      mkdir "$dir"
      cp test.c "$dir/test.c"
    done

    cd obj1
    gcda_cycle -fprofile-prefix-path=/you/shall/not/pass -fprofile-dir=. 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    cd ../obj2
    gcda_cycle -fprofile-prefix-path=/you/shall/not/pass -fprofile-dir=. 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS_OLD"
}
