# =============================================================================

SUITE_preprocessor_SETUP() {
    unset CCACHE_NODIRECT
}

SUITE_preprocessor() {
    preprocessor_tests
}

# =============================================================================

preprocessor_tests() {
    # -------------------------------------------------------------------------
    TEST "Check use of preprocessor different than compiler"

    cat >test1.c <<'EOF'
source
EOF

    cat >preprocessor.sh <<'EOF'
#!/bin/sh
if [ "$1" = "-E" ]; then
    echo preprocessed
    printf ${N}Pu >&$UNCACHED_ERR_FD
else
    exit 1
fi
EOF
    chmod +x preprocessor.sh

    cat >compiler.sh <<'EOF'
#!/bin/sh
if [ "$1" = "-E" ]; then
    exit 1
else
    echo compiled >test1.o
    printf ${N}Cc >&2
    printf ${N}Cu >&$UNCACHED_ERR_FD
fi
EOF
    chmod +x compiler.sh

    N=1 CCACHE_PREPROCESSOR=./preprocessor.sh $CCACHE ./compiler.sh -c test1.c 2>stderr1
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content stderr1 "1Pu1Cu1Cc"
    expect_content test1.o "compiled"
    rm test1.o

    N=2 CCACHE_PREPROCESSOR=./preprocessor.sh $CCACHE ./compiler.sh -c test1.c 2>stderr2
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content stderr2 "1Cc"
    expect_content test1.o "compiled"
    rm test1.o

    echo "_change" >> test1.c
    N=3 CCACHE_DEBUG=1 CCACHE_PREPROCESSOR=./preprocessor.sh $CCACHE ./compiler.sh -c test1.c 2>stderr3
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content stderr3 "3Pu1Cc"
    expect_content test1.o "compiled"
    rm test1.o
}
