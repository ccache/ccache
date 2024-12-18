SUITE_debug_compilation_dir_PROBE() {
    touch test.c
    if ! $COMPILER -c -fdebug-compilation-dir=dir test.c 2>/dev/null; then
        echo "-fdebug-compilation-dir not supported by compiler"
    fi

    if ! $RUN_WIN_XFAIL; then
        echo "debug-compilation-dir tests are broken on Windows."
        return
    fi
}

SUITE_debug_compilation_dir_SETUP() {
    unset CCACHE_NODIRECT

    mkdir -p dir1/src dir1/include
    cat <<EOF >dir1/src/test.c
#include <stdarg.h>
#include <test.h>
EOF
    cat <<EOF >dir1/include/test.h
int test;
EOF
    cp -r dir1 dir2
    backdate dir1/include/test.h dir2/include/test.h
}

SUITE_debug_compilation_dir() {
    # -------------------------------------------------------------------------
    TEST "Setting compilation directory"

    cd dir1
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -g -fdebug-compilation-dir some_name_not_likely_to_exist_in_path -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_objdump_not_contains test.o "$(pwd)"
    expect_objdump_contains test.o some_name_not_likely_to_exist_in_path

    cd ../dir2
    CCACHE_BASEDIR=$(pwd) $CCACHE_COMPILE -I$(pwd)/include -g -fdebug-compilation-dir some_name_not_likely_to_exist_in_path -c $(pwd)/src/test.c -o $(pwd)/test.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_objdump_not_contains test.o "$(pwd)"
    expect_objdump_contains test.o some_name_not_likely_to_exist_in_path
}
