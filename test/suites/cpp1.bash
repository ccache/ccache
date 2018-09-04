SUITE_cpp1_PROBE() {
    touch test.c
    if $COMPILER_TYPE_GCC; then
        if ! $REAL_COMPILER -E -fdirectives-only test.c >&/dev/null; then
            echo "-fdirectives-only not supported by compiler"
            return
        fi
    elif $COMPILER_TYPE_CLANG; then
        if ! $REAL_COMPILER -E -frewrite-includes test.c >&/dev/null; then
            echo "-frewrite-includes not supported by compiler"
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

    $REAL_COMPILER $cpp_flag -c -o reference_test1.o test1.c

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    unset CCACHE_NODIRECT

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE $cpp_flag -c test1.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_equal_object_files reference_test1.o test1.o
}
