SUITE_distributed_thinlto_clang_PROBE() {
    echo 'int main() { return 0; }' >test.c
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
    elif ! $COMPILER -fuse-ld=lld test.c 2>/dev/null; then
        echo "compiler does not support lld"
    elif ! $COMPILER --help | grep -q -- -fthinlto-index 2>/dev/null; then
        echo "compiler does not support thinlto-index"
    fi
}

SUITE_distributed_thinlto_clang_SETUP() {
    cat <<EOF >test.c
#include <stdio.h>

// define in lib1.c
int lib1();
// define in lib2.c
int lib2();
int main(void) { printf("result: %d\\n", lib1() + lib2()); return 0;}
EOF
    cat <<EOF >lib1.c
int lib1() { return 1; }
EOF
    cat <<EOF >lib2.c
int lib2() { return 2; }
EOF
    $COMPILER -flto=thin -O2 -c -o test.o test.c
    $COMPILER -flto=thin -O2 -c -o lib1.o lib1.c
    $COMPILER -flto=thin -O2 -c -o lib2.o lib2.c
    # Use compiler to generate thinlto.index.bc.
    $COMPILER -fuse-ld=lld -Wl,--thinlto-index-only=test.rst test.o lib1.o lib2.o
    backdate *.c *.o *.bc
    unset CCACHE_NODIRECT
}

SUITE_distributed_thinlto_clang() {
    # -------------------------------------------------------------------------
    TEST "-fthinlto-index=test.o.thinlto.bc"

    $CCACHE_COMPILE -c -fthinlto-index=lib1.o.thinlto.bc lib1.o -o lib1.native.o
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -fthinlto-index=lib1.o.thinlto.bc lib1.o -o lib1.native.o
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -fthinlto-index=test.o.thinlto.bc test.o -o test.native.o
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c -fthinlto-index=test.o.thinlto.bc test.o -o test.native.o
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2

    # Modify lib2.c, only affect test.o.thinlto.bc, but not affect lib1.o.thinlto.bc.
    echo 'int tmp_x;' >>lib2.c

    $COMPILER -O2 -flto=thin -c -o lib2.o lib2.c
    $COMPILER -fuse-ld=lld -Wl,--thinlto-index-only=test.rst test.o lib1.o lib2.o
    backdate *.c *.o *.bc

    $CCACHE_COMPILE -c -fthinlto-index=lib1.o.thinlto.bc lib1.o -o lib1.native.o
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c -fthinlto-index=test.o.thinlto.bc test.o -o test.native.o
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
}
