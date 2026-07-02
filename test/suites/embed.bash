SUITE_embed_PROBE() {
    if ! $COMPILER_TYPE_CLANG; then
        echo "compiler is not Clang"
        return
    fi

    echo "probe" >embed_probe.bin
    cat >embed_probe.c <<'EOF'
const char x[] = {
#embed "embed_probe.bin"
};
EOF

    for std in c23 gnu2x; do
        if $COMPILER -std=$std -c embed_probe.c -o embed_probe.o 2>/dev/null; then
            return
        fi
    done

    echo "compiler does not support C23 #embed"
}

SUITE_embed_SETUP() {
    unset CCACHE_NODIRECT

    printf 'probe' >test.bin
    cat >test.c <<'EOF'
const char embedded[] = {
#embed "test.bin"
};
EOF
}

SUITE_embed() {
    # -------------------------------------------------------------------------
    TEST "#embed file change invalidates cache"

    local std_flag=
    for std in c23 gnu2x; do
        if $COMPILER -std=$std -c test.c -o /dev/null 2>/dev/null; then
            std_flag="-std=$std"
            break
        fi
    done
    if [ -z "$std_flag" ]; then
        test_failed_internal "compiler does not support C23 #embed"
    fi

    printf 'embed_content_aaaa' >test.bin
    $COMPILER $std_flag -c test.c -o reference_a.o \
        || test_failed_internal "failed to compile reference_a.o"

    printf 'embed_content_bbbb' >test.bin
    $COMPILER $std_flag -c test.c -o reference_b.o \
        || test_failed_internal "failed to compile reference_b.o"
    expect_different_content reference_a.o reference_b.o

    printf 'embed_content_aaaa' >test.bin
    $CCACHE_COMPILE $std_flag -c test.c -o test_a.o
    expect_stat cache_miss 1
    expect_equal_object_files reference_a.o test_a.o

    printf 'embed_content_bbbb' >test.bin
    $CCACHE_COMPILE $std_flag -c test.c -o test_b.o
    expect_equal_object_files reference_b.o test_b.o
}
