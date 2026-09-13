SUITE_ispc_PROBE() {
    if [ -z "$(find_compiler ispc)" ]; then
        echo "ispc is not available"
    fi
}

SUITE_ispc_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >test.isph
#define SCALE 3
EOF

    cat <<EOF >test.ispc
#include "test.isph"

export void scale(uniform float data[], uniform int count) {
    foreach (i = 0 ... count) {
        data[i] *= SCALE;
    }
}
EOF
}

SUITE_ispc() {
    local real_ispc
    real_ispc=$(find_compiler ispc)
    ispc_single="$CCACHE $real_ispc --target=avx2-i32x8"
    ispc_multi="$CCACHE $real_ispc --target=sse4.2-i32x4,avx2-i32x8"

    # -------------------------------------------------------------------------
    TEST "Single target"

    $ispc_single -o test.o test.ispc
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_exists test.o

    cp test.o reference.o
    rm test.o

    $ispc_single -o test.o test.ispc
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content reference.o test.o

    # -------------------------------------------------------------------------
    TEST "Header output"

    $ispc_single -o test.o -h test_ispc.h test.ispc
    expect_stat cache_miss 1
    cp test_ispc.h reference_ispc.h
    rm test.o test_ispc.h

    $ispc_single -o test.o -h test_ispc.h test.ispc
    expect_stat direct_cache_hit 1
    expect_exists test.o
    expect_equal_content reference_ispc.h test_ispc.h

    # -------------------------------------------------------------------------
    TEST "Multiple targets"

    $ispc_multi -o test.o -h test_ispc.h test.ispc
    expect_stat cache_miss 1
    for file in test.o test_sse4.o test_avx2.o \
                test_ispc.h test_ispc_sse4.h test_ispc_avx2.h; do
        expect_exists $file
        cp $file reference_$file
        rm $file
    done

    $ispc_multi -o test.o -h test_ispc.h test.ispc
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    for file in test.o test_sse4.o test_avx2.o \
                test_ispc.h test_ispc_sse4.h test_ispc_avx2.h; do
        expect_equal_content reference_$file $file
    done

    # -------------------------------------------------------------------------
    TEST "Dependency file via -M -MT -MF"

    $ispc_single -o test.o -M -MT test.o -MF test.o.d test.ispc
    expect_stat cache_miss 1
    expect_contains test.o.d "test.isph"
    cp test.o.d reference.o.d
    rm test.o test.o.d

    $ispc_single -o test.o -M -MT test.o -MF test.o.d test.ispc
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content reference.o.d test.o.d

    # -------------------------------------------------------------------------
    TEST "Dependency file via -MMM"

    $ispc_single -o test.o -MMM deps.txt test.ispc
    expect_stat cache_miss 1
    expect_contains deps.txt "test.isph"
    cp deps.txt reference_deps.txt
    rm test.o deps.txt

    $ispc_single -o test.o -MMM deps.txt test.ispc
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content reference_deps.txt deps.txt

    # -------------------------------------------------------------------------
    TEST "-M without -MF writes the rule to stdout"

    $ispc_single -o test.o -M test.ispc >stdout.txt
    expect_stat cache_miss 1
    expect_contains stdout.txt "test.isph"
    expect_missing test.o.d
    rm test.o

    $ispc_single -o test.o -M test.ispc >stdout2.txt
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content stdout.txt stdout2.txt

    # -------------------------------------------------------------------------
    TEST "Modified include invalidates the entry"

    $ispc_single -o test.o -M -MT test.o -MF test.o.d test.ispc
    expect_stat cache_miss 1

    echo "#define SCALE 4" >test.isph
    backdate test.isph

    $ispc_single -o test.o -M -MT test.o -MF test.o.d test.ispc
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
}
