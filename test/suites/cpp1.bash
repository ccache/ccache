SUITE_cpp1_PROBE() {
    touch test.c
    if $COMPILER_TYPE_GCC; then
        if ! $COMPILER -E -fdirectives-only test.c >&/dev/null; then
            echo "-fdirectives-only not supported by compiler"
            return
        fi
    elif $COMPILER_TYPE_CLANG; then
        if ! $COMPILER -E -frewrite-includes test.c >&/dev/null; then
            echo "-frewrite-includes not supported by compiler"
            return
        fi
        if $HOST_OS_WINDOWS && ! $COMPILER_USES_MSVC; then
            echo "This test is broken on msys2 clang: Stores wrong file names like 'tmp.cpp_stdout.2Gq' instead of 'test1.c'."
            return
        fi
    else
        echo "Unknown compiler: $COMPILER"
        return
    fi
}

SUITE_cpp1_SETUP() {
    export CCACHE_NOCPP2=1
    echo "#define FOO 1" >test1.h
    backdate test1.h
    echo '#include "test1.h"' >test1.c
    echo '#define BAR 2' >>test1.c
    echo 'int foo(int x) { return FOO; }' >>test1.c
    echo 'int bar(int x) { return BAR; }' >>test1.c
    echo 'int baz(int x) { return BAZ; }' >>test1.c
}

SUITE_cpp1() {
    if $COMPILER_TYPE_GCC; then
        cpp_flag="-fdirectives-only"
    elif $COMPILER_TYPE_CLANG; then
        cpp_flag="-frewrite-includes"
    fi
    cpp_flag="$cpp_flag -DBAZ=3"

    # -------------------------------------------------------------------------
    TEST "Base case"

    $COMPILER $cpp_flag -c -o reference_test1.o test1.c

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    unset CCACHE_NODIRECT

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_equal_object_files reference_test1.o test1.o
}
