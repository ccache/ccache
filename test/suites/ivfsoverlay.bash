SUITE_ivfsoverlay_PROBE() {
    if ! $COMPILER_TYPE_CLANG; then
        echo "-ivfsoverlay not supported by compiler"
    else
        touch test.c
        cat <<EOF >test.yaml
{"case-sensitive":"false","roots":[],"version":0}
EOF
        $COMPILER -ivfsoverlay test.yaml test.c -S || echo "compiler does not support -ivfsoverlay"
    fi
}

SUITE_ivfsoverlay_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >test.yaml
{"case-sensitive":"false","roots":[],"version":0}
EOF
    cat <<EOF >test.c
// test.c
int test;
EOF
}

SUITE_ivfsoverlay() {
    # -------------------------------------------------------------------------
    TEST "without sloppy ivfsoverlay"

    $CCACHE_COMPILE -ivfsoverlay test.yaml -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_compiler_option 1

    # -------------------------------------------------------------------------
    TEST "with sloppy ivfsoverlay"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime ivfsoverlay" $CCACHE_COMPILE -ivfsoverlay test.yaml -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS include_file_mtime ivfsoverlay" $CCACHE_COMPILE -ivfsoverlay test.yaml -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
}
