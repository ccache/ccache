SUITE_masquerading_PROBE() {
    local compiler_binary
    if $HOST_OS_WINDOWS || $HOST_OS_CYGWIN; then
        echo "symlinks not supported on $(uname -s)"
        return
    fi
    if [ "$(dirname $COMPILER_BIN)" != . ]; then
        echo "compiler ($COMPILER_BIN) not taken from PATH"
        return
    fi
}

SUITE_masquerading_SETUP() {
    generate_code 1 test1.c
}

SUITE_masquerading() {
if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink, relative path"

    ln -s "$CCACHE" $COMPILER_BIN
    $COMPILER -c -o reference_test1.o test1.c

    PATH="${PWD}:${PATH}" ./$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    PATH="${PWD}:${PATH}" ./$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink, absolute path"

    ln -s "$CCACHE" $COMPILER_BIN
    $COMPILER -c -o reference_test1.o test1.c

    PATH="${PWD}:${PATH}" $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    PATH="${PWD}:${PATH}" $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink, ccache as launcher"

    ln -s "$CCACHE" $COMPILER_BIN
    $COMPILER -c -o reference_test1.o test1.c

    PATH="${PWD}:${PATH}" $CCACHE $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    PATH="${PWD}:${PATH}" $CCACHE $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Masquerading via hard link, ccache as launcher"

    ln "$CCACHE" $COMPILER_BIN
    $COMPILER -c -o reference_test1.o test1.c

    PATH="${PWD}:${PATH}" $CCACHE $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    PATH="${PWD}:${PATH}" $CCACHE $PWD/$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink, ccache as launcher, compiler change"

    # Two distinct compilers behind the masquerade name, simulating a compiler
    # upgrade: same preprocessed output but different objects, and differing
    # wrapper sizes so that compiler_check = mtime can distinguish them.
    mkdir wrapper-a wrapper-b
    printf '#!/bin/sh\nexec %s %s -O0 "$@"\n' \
        "$REAL_COMPILER_BIN" "$COMPILER_ARGS" >wrapper-a/$COMPILER_BIN
    printf '#!/bin/sh\n# optimized\nexec %s %s -O2 "$@"\n' \
        "$REAL_COMPILER_BIN" "$COMPILER_ARGS" >wrapper-b/$COMPILER_BIN
    chmod +x wrapper-a/$COMPILER_BIN wrapper-b/$COMPILER_BIN

    cat >fixture.c <<EOF
int fixture_sum(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}
EOF

    $COMPILER -O0 -c -o reference_o0.o fixture.c
    $COMPILER -O2 -c -o reference_o2.o fixture.c

    ln -s "$CCACHE" $COMPILER_BIN
    PATH="${PWD}/wrapper-a:${PATH}" \
        $CCACHE $PWD/$COMPILER_BIN -c -o test1.o fixture.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_object_files reference_o0.o test1.o

    PATH="${PWD}/wrapper-b:${PATH}" \
        $CCACHE $PWD/$COMPILER_BIN -c -o test1.o fixture.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_equal_object_files reference_o2.o test1.o

    # -------------------------------------------------------------------------
    TEST "Masquerading via symlink, ccache as launcher, compiler change, content check"

    export CCACHE_COMPILERCHECK=content

    mkdir wrapper-a wrapper-b
    printf '#!/bin/sh\nexec %s %s -O0 "$@"\n' \
        "$REAL_COMPILER_BIN" "$COMPILER_ARGS" >wrapper-a/$COMPILER_BIN
    printf '#!/bin/sh\n# optimized\nexec %s %s -O2 "$@"\n' \
        "$REAL_COMPILER_BIN" "$COMPILER_ARGS" >wrapper-b/$COMPILER_BIN
    chmod +x wrapper-a/$COMPILER_BIN wrapper-b/$COMPILER_BIN

    cat >fixture.c <<EOF
int fixture_sum(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}
EOF

    $COMPILER -O0 -c -o reference_o0.o fixture.c
    $COMPILER -O2 -c -o reference_o2.o fixture.c

    ln -s "$CCACHE" $COMPILER_BIN
    PATH="${PWD}/wrapper-a:${PATH}" \
        $CCACHE $PWD/$COMPILER_BIN -c -o test1.o fixture.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_object_files reference_o0.o test1.o

    PATH="${PWD}/wrapper-b:${PATH}" \
        $CCACHE $PWD/$COMPILER_BIN -c -o test1.o fixture.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_equal_object_files reference_o2.o test1.o
fi

    # -------------------------------------------------------------------------
    TEST "Masquerading via copy or hard link"

    cp "$CCACHE" $COMPILER_BIN
    $COMPILER -c -o reference_test1.o test1.c

    PATH="${PWD}:${PATH}" ./$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    PATH="${PWD}:${PATH}" ./$COMPILER_BIN $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o
}
