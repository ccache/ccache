# =============================================================================

SUITE_normalize_whitespace_PROBE() {
 	echo "int main() { return 0; }" | $REAL_COMPILER -E -fnormalize-whitespace - -o /dev/null
    if [ $? -ne 0 ]; then
        echo "-fnormalize-whitespace not supported by $REAL_COMPILER"
    fi
}

SUITE_normalize_whitespace_SETUP() {
    cat >compiler.sh <<'EOF'
#! /usr/bin/env bash
while [ $# -gt 0 ]; do
    case $1 in
        -o)
            DST=$2
            shift 2
            ;;
        -MF)
            shift 2
            ;;
        -*)
            shift
            ;;
        *)
            SRC=$1
            shift
            ;;
    esac
done
cat $SRC >> $DST
EOF
    chmod +x compiler.sh

    cat <<EOF >normalize.c
// normalize.c
int main() { return 0; }
EOF
}

SUITE_normalize_whitespace() {
    normalize_whitespace_tests
}

# =============================================================================

normalize_whitespace_tests() {
    # -------------------------------------------------------------------------
    TEST "Preprocess mode"

    # Reference output
    $REAL_COMPILER -c normalize.c -o normalize-ref.o

    # Expect miss with empty cache
    $CCACHE_COMPILE -c normalize.c -o normalize1.o
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_object_files normalize-ref.o normalize1.o

    # Expect hit when running again
    $CCACHE_COMPILE -c normalize.c -o normalize2.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_equal_object_files normalize-ref.o normalize2.o

    # Expect hit with whitespace-only change (on the same line)
    cat <<EOF >normalize.c
// normalize.c
int      main( )/* COMMENT */{return 0;} // COMMENT
EOF
    $CCACHE $CCACHE_COMPILE -c normalize.c -o normalize3.o
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_equal_object_files normalize-ref.o normalize3.o

    # Expect miss with line change
    cat <<EOF >normalize.c
// normalize.c
int main()
{ return 0; }
EOF
    $CCACHE_COMPILE -c normalize.c -o normalize4.o
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2
    expect_equal_object_files normalize-ref.o normalize4.o


    # -------------------------------------------------------------------------
    TEST "Unify mode"

    # Reference output
    $REAL_COMPILER -c normalize.c -o normalize-ref.o

    # Expect miss with empty cache
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c normalize.c -o normalize1.o
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_equal_object_files normalize-ref.o normalize1.o

    # Expect hit with whitespace change (including lines)
    cat <<EOF >normalize.c
// normalize.c
int   main( )/* COMMENT */{
      return 0;
} // COMMENT
EOF
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c normalize.c -o normalize2.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_equal_object_files normalize-ref.o normalize2.o

    # Expect miss with non-whitespace change
    cat <<EOF >normalize.c
// normalize.c
int main() { return 1; }
EOF
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c normalize.c -o normalize3.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2


    # -------------------------------------------------------------------------
    TEST "Use $REAL_COMPILER -fnormalize-whitespace as preprocessor only"

    # Reference output
    cp normalize.c normalize-ref.o

    # Expect miss with empty cache
    CCACHE_PREPROCESSOR="$REAL_COMPILER" CCACHE_UNIFY=1 $CCACHE ./compiler.sh -c normalize.c -o normalize1.o
    expect_equal_content normalize-ref.o normalize1.o
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # Expect hit with whitespace change (including lines)
    cat <<EOF >normalize.c
// normalize.c
int   main( )/* COMMENT */{
      return 0;
} // COMMENT
EOF
    CCACHE_PREPROCESSOR="$REAL_COMPILER" CCACHE_UNIFY=1 $CCACHE ./compiler.sh  -c normalize.c -o normalize2.o
    expect_equal_content normalize-ref.o normalize2.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # Expect miss with non-whitespace change
    cat <<EOF >normalize.c
// normalize.c
int main() { return 1; }
EOF
    CCACHE_PREPROCESSOR="$REAL_COMPILER" CCACHE_UNIFY=1 $CCACHE ./compiler.sh -c normalize.c -o normalize3.o
    expect_equal_content normalize.c normalize3.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
}
