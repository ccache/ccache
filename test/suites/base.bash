base_tests() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 0
    expect_stat preprocessed_cache_miss 1
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 1
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat direct_cache_miss 0
    expect_stat preprocessed_cache_miss 1
    expect_stat primary_storage_hit 1
    expect_stat primary_storage_miss 1
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "ccache ccache gcc"
    # E.g. due to some suboptimal setup, scripts etc. Source:
    # https://github.com/ccache/ccache/issues/686

    $COMPILER -c -o reference_test1.o test1.c

    $CCACHE $COMPILER -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE $CCACHE $COMPILER -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE $CCACHE $CCACHE $COMPILER -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Version output readable"

    # The exact output is not tested, but at least it's something human readable
    # and not random memory.
    local version_pattern=$'^ccache version [a-zA-Z0-9_./+-]*\r?$'
    if [ $($CCACHE --version | grep -E -c "$version_pattern") -ne 1 ]; then
        test_failed "Unexpected output of --version"
    fi

    # -------------------------------------------------------------------------
    TEST "Debug option"

    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $COMPILER -c -o reference_test1.o test1.c -g
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Output option"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c -o foo.o
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o foo.o

    # -------------------------------------------------------------------------
    TEST "Output option without space"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c -odir
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c -optf
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    $COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o dir
    expect_equal_object_files reference_test1.o ptf

    # -------------------------------------------------------------------------
    TEST "Called for link"

    $CCACHE_COMPILE test1.c -o test 2>/dev/null
    expect_stat called_for_link 1

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE test1.o -o test 2>/dev/null
    expect_stat called_for_link 2

    # -------------------------------------------------------------------------
    TEST "No existing input file"

    $CCACHE_COMPILE -c foo.c 2>/dev/null
    expect_stat no_input_file 1

    # -------------------------------------------------------------------------
    TEST "No input file on command line"

    $CCACHE_COMPILE -c -O2 2>/dev/null
    expect_stat no_input_file 1

    # -------------------------------------------------------------------------
    TEST "Called for preprocessing"

    $CCACHE_COMPILE -E -c test1.c >/dev/null 2>&1
    expect_stat called_for_preprocessing 1

    # -------------------------------------------------------------------------
    TEST "Multiple source files"

    touch test2.c
    $CCACHE_COMPILE -c test1.c test2.c
    expect_stat multiple_source_files 1

    # -------------------------------------------------------------------------
    TEST "Couldn't find the compiler"

    $CCACHE blahblah -c test1.c 2>/dev/null
    exit_code=$?
    if [ $exit_code -ne 1 ]; then
        test_failed "Expected exit code to be 1, actual value $exit_code"
    fi

    # -------------------------------------------------------------------------
    TEST "Bad compiler arguments"

    $CCACHE_COMPILE -c test1.c -I 2>/dev/null
    expect_stat bad_compiler_arguments 1

    # -------------------------------------------------------------------------
    TEST "Unsupported source language"

    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2>/dev/null
    expect_stat unsupported_source_language 1

    # -------------------------------------------------------------------------
    TEST "Unsupported compiler option"

    $CCACHE_COMPILE -M foo -c test1.c >/dev/null 2>&1
    expect_stat unsupported_compiler_option 1

    # -------------------------------------------------------------------------
    for variable in DEPENDENCIES_OUTPUT SUNPRO_DEPENDENCIES; do
        TEST "Unsupported environment variable ${variable}"

        eval "export ${variable}=1"
        $CCACHE_COMPILE -c test1.c
        expect_stat unsupported_environment_variable 1
    done

    # -------------------------------------------------------------------------
    TEST "Output to directory"

    mkdir testd
    $CCACHE_COMPILE -o testd -c test1.c >/dev/null 2>&1
    rmdir testd >/dev/null 2>&1
    expect_stat bad_output_file 1

    # -------------------------------------------------------------------------
    TEST "Output to file in nonexistent directory"

    mkdir out

    $CCACHE_COMPILE -c test1.c -o out/foo.o
    expect_stat bad_output_file 0
    expect_stat cache_miss 1

    rm -rf out

    $CCACHE_COMPILE -c test1.c -o out/foo.o 2>/dev/null
    expect_stat bad_output_file 1
    expect_stat cache_miss 1
    expect_missing out/foo.o

    # -------------------------------------------------------------------------
    TEST "No file extension"

    mkdir src
    touch src/foo

    $CCACHE_COMPILE -x c -c src/foo
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists foo.o
    rm foo.o

    $CCACHE_COMPILE -x c -c src/foo
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_exists foo.o
    rm foo.o

    rm -rf src

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS; then
    TEST "Source file ending with dot"

    mkdir src
    touch src/foo.

    $CCACHE_COMPILE -x c -c src/foo.
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists foo.o
    rm foo.o

    $CCACHE_COMPILE -x c -c src/foo.
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_exists foo.o
    rm foo.o

    rm -rf src
fi

    # -------------------------------------------------------------------------
    TEST "Multiple file extensions"

    mkdir src
    touch src/foo.c.c

    $CCACHE_COMPILE -c src/foo.c.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists foo.c.o
    rm foo.c.o

    $CCACHE_COMPILE -c src/foo.c.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_exists foo.c.o
    rm foo.c.o

    rm -rf src

    # -------------------------------------------------------------------------
    TEST "LANG"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 2

    LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 2

    # -------------------------------------------------------------------------
    TEST "LANG with sloppiness"

    CCACHE_SLOPPINESS=locale LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    CCACHE_SLOPPINESS=locale LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    CCACHE_SLOPPINESS=locale LANG=bar $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    # -------------------------------------------------------------------------
    TEST "Result file is compressed"

    $CCACHE_COMPILE -c test1.c
    result_file=$(find $CCACHE_DIR -name '*R')
    if ! $CCACHE --inspect $result_file | grep 'Compression type: zstd' >/dev/null 2>&1; then
        test_failed "Result file not uncompressed according to metadata"
    fi
    if [ $(file_size $result_file) -ge $(file_size test1.o) ]; then
        test_failed "Result file seems to be uncompressed"
    fi

    # -------------------------------------------------------------------------
    TEST "Corrupt result file"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    result_file=$(find $CCACHE_DIR -name '*R')
    printf foo | dd of=$result_file bs=3 count=1 seek=20 conv=notrunc >&/dev/null

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 1

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
    expect_stat files_in_cache 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_DEBUG"

    unset CCACHE_LOGFILE
    unset CCACHE_NODIRECT
    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test1.c
    if ! grep -q Result: test1.o.*.ccache-log; then
        test_failed "Unexpected data in <obj>.ccache-log"
    fi
    if ! grep -q "PREPROCESSOR MODE" test1.o.*.ccache-input-text; then
        test_failed "Unexpected data in <obj>.<timestamp>.ccache-input-text"
    fi
    for ext in c p d; do
        if ! [ -f test1.o.*.ccache-input-$ext ]; then
            test_failed "<obj>.ccache-input-$ext missing"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "CCACHE_DEBUG with too hard option"

    CCACHE_DEBUG=1 $CCACHE_COMPILE -c test1.c -save-temps
    expect_stat unsupported_compiler_option 1
    expect_exists test1.o.*.ccache-log

    # -------------------------------------------------------------------------
    TEST "CCACHE_DEBUGDIR"

    CCACHE_DEBUG=1 CCACHE_DEBUGDIR=debugdir $CCACHE_COMPILE -c test1.c
    expect_contains debugdir"$(pwd -P)"/test1.o.*.ccache-log "Result: cache_miss"

    # -------------------------------------------------------------------------
    TEST "CCACHE_DISABLE"

    CCACHE_DISABLE=1 $CCACHE_COMPILE -c test1.c 2>/dev/null
    if [ -d $CCACHE_DIR ]; then
        test_failed "$CCACHE_DIR created despite CCACHE_DISABLE being set"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMMENTS"

    $COMPILER -c -o reference_test1.o test1.c

    mv test1.c test1-saved.c
    echo '// initial comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    echo '// different comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    mv test1-saved.c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOSTATS"

    CCACHE_NOSTATS=1 $CCACHE_COMPILE -c test1.c -O -O
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0

    # -------------------------------------------------------------------------
    TEST "stats file forward compatibility"

    mkdir -p "$CCACHE_DIR/4/"
    stats_file="$CCACHE_DIR/4/stats"
    touch "$CCACHE_DIR/timestamp_reference"

    for i in `seq 101`; do
       echo $i
    done > "$stats_file"

    expect_stat cache_miss 5
    $CCACHE_COMPILE -c test1.c
    expect_stat cache_miss 6
    expect_contains "$stats_file" 101
    expect_newer_than "$stats_file" "$CCACHE_DIR/timestamp_reference"

    # -------------------------------------------------------------------------
    TEST "stats file with large counter values"

    mkdir -p "$CCACHE_DIR/4/"
    stats_file="$CCACHE_DIR/4/stats"

    echo "0 0 0 0 1234567890123456789" >"$stats_file"

    expect_stat cache_miss 1234567890123456789
    $CCACHE_COMPILE -c test1.c
    expect_stat cache_miss 1234567890123456790

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat recache 0

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat recache 1

    $COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    expect_stat files_in_cache 1

    # -------------------------------------------------------------------------
    TEST "Directory is hashed if using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if not using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if using -g -g0"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if using -gz"

    $COMPILER -E test1.c -gz >preprocessed.i 2>/dev/null
    if [ -s preprocessed.i ] && ! fgrep -q $PWD preprocessed.i; then
        mkdir dir1 dir2
        cp test1.c dir1
        cp test1.c dir2

        cd dir1
        $CCACHE_COMPILE -c test1.c -gz
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1
        $CCACHE_COMPILE -c test1.c -gz
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1

        cd ../dir2
        $CCACHE_COMPILE -c test1.c -gz
        expect_stat preprocessed_cache_hit 2
        expect_stat cache_miss 1
    fi

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if using -gz=zlib"

    $COMPILER -E test1.c -gz=zlib >preprocessed.i 2>/dev/null
    if [ -s preprocessed.i ] && ! fgrep -q $PWD preprocessed.i; then
        mkdir dir1 dir2
        cp test1.c dir1
        cp test1.c dir2

        cd dir1
        $CCACHE_COMPILE -c test1.c -gz=zlib
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1
        $CCACHE_COMPILE -c test1.c -gz=zlib
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1

        cd ../dir2
        $CCACHE_COMPILE -c test1.c -gz=zlib
        expect_stat preprocessed_cache_hit 2
        expect_stat cache_miss 1
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOHASHDIR"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    cd ../dir2
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_EXTRAFILES"

    echo "a" >a
    echo "b" >b

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    echo b2 >b

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 3

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 3
    expect_stat cache_miss 3

    CCACHE_EXTRAFILES="doesntexist" $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 3
    expect_stat cache_miss 3
    expect_stat error_hashing_extra_file 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_PREFIX"

    cat <<'EOF' >prefix-a
#!/bin/sh
echo a >prefix.result
exec "$@"
EOF
    cat <<'EOF' >prefix-b
#!/bin/sh
echo b >>prefix.result
exec "$@"
EOF
    chmod +x prefix-a prefix-b
    cat <<'EOF' >file.c
int foo;
EOF
    PATH=.:$PATH CCACHE_PREFIX="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content prefix.result "a
b"

    PATH=.:$PATH CCACHE_PREFIX="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content prefix.result "a
b"

    rm -f prefix.result
    PATH=.:$PATH CCACHE_PREFIX_CPP="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_content prefix.result "a
b"

    # -------------------------------------------------------------------------
    TEST "Files in cache"

    for i in $(seq 32); do
        generate_code $i test$i.c
        $CCACHE_COMPILE -c test$i.c
    done
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 32
    expect_stat files_in_cache 32

    # -------------------------------------------------------------------------
    TEST "Direct .i compile"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -c test1.c -E >test1.i
    $CCACHE_COMPILE -c test1.i
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-x c"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-xc"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-x none"

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-x unknown"

    $CCACHE_COMPILE -x unknown -c test1.c 2>/dev/null
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_source_language 1

    # -------------------------------------------------------------------------
    TEST "-x c -c /dev/null"

    $CCACHE_COMPILE -x c -c /dev/null -o null.o 2>/dev/null
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -x c -c /dev/null -o null.o 2>/dev/null
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-D not hashed"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -DNOT_AFFECTING=1 -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-S"

    $CCACHE_COMPILE -S test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -S test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.s
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    $CCACHE_COMPILE -c test1.s
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "-frecord-gcc-switches"

    if $COMPILER -frecord-gcc-switches -c test1.c >&/dev/null; then
        $CCACHE_COMPILE -frecord-gcc-switches -c test1.c
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1

        $CCACHE_COMPILE -frecord-gcc-switches -c test1.c
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1

        $CCACHE_COMPILE -frecord-gcc-switches -Wall -c test1.c
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 2

        $CCACHE_COMPILE -frecord-gcc-switches -Wall -c test1.c
        expect_stat preprocessed_cache_hit 2
        expect_stat cache_miss 2
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_DISABLE set when executing compiler"

    cat >compiler.sh <<EOF
#!/bin/sh
printf "%s" "\${CCACHE_DISABLE}" >>CCACHE_DISABLE.value
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh
    backdate compiler.sh
    $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content CCACHE_DISABLE.value '11' # preprocessor + compiler

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILER"

    $COMPILER -c -o reference_test1.o test1.c

    export CCACHE_DEBUG=1
    export CCACHE_DEBUGDIR=/tmp/a
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    export CCACHE_DEBUGDIR=/tmp/b
    CCACHE_COMPILER=$COMPILER $CCACHE \
        non_existing_compiler_will_be_overridden_anyway \
        $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    CCACHE_COMPILER=$COMPILER $CCACHE same/for/relative \
        $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    CCACHE_COMPILER=$COMPILER $CCACHE /and/even/absolute/compilers \
        $COMPILER_ARGS -c test1.c
    expect_stat preprocessed_cache_hit 3
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERTYPE"

    $CCACHE_COMPILE -c test1.c
    cat >gcc <<EOF
#!/bin/sh
EOF
    chmod +x gcc

    CCACHE_DEBUG=1 $CCACHE ./gcc -c test1.c
    compiler_type=$(sed -En 's/.*Compiler type: (.*)/\1/p' test1.o.*.ccache-log)
    if [ "$compiler_type" != gcc ]; then
        test_failed "Compiler type $compiler_type != gcc"
    fi

    rm test1.o.*.ccache-log

    CCACHE_COMPILERTYPE=clang CCACHE_DEBUG=1 $CCACHE ./gcc -c test1.c
    compiler_type=$(sed -En 's/.*Compiler type: (.*)/\1/p' test1.o.*.ccache-log)
    if [ "$compiler_type" != clang ]; then
        test_failed "Compiler type $compiler_type != clang"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_PATH"

    override_path=`pwd`/override_path
    mkdir $override_path
    cat >$override_path/cc <<EOF
#!/bin/sh
touch override_path_compiler_executed
EOF
    chmod +x $override_path/cc
    CCACHE_PATH=$override_path $CCACHE cc -c test1.c
    if [ ! -f override_path_compiler_executed ]; then
        test_failed "CCACHE_PATH had no effect"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=mtime"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
# A comment
EOF
    chmod +x compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    sed_in_place 's/comment/yoghurt/' compiler.sh # Don't change the size
    chmod +x compiler.sh
    backdate compiler.sh # Don't change the timestamp

    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    touch compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=content"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    echo "# Compiler upgrade" >>compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=none"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=string"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=command"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK='echo %compiler%' $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK="echo ./compiler.sh" $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    cat <<EOF >foobar.sh
#!/bin/sh
echo foo
echo bar
EOF
    chmod +x foobar.sh
    CCACHE_COMPILERCHECK='./foobar.sh' $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_COMPILERCHECK='echo foo; echo bar' $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=unknown_command"

    cat >compiler.sh <<EOF
#!/bin/sh
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK="unknown_command" $CCACHE ./compiler.sh -c test1.c 2>/dev/null
    expect_stat compiler_check_failed 1


    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS; then
    TEST "CCACHE_UMASK"

    saved_umask=$(umask)
    umask 022
    export CCACHE_UMASK=002
    export CCACHE_TEMPDIR=$CCACHE_DIR/tmp

    cat <<EOF >test.c
int main() {}
EOF

    # A cache-miss case which affects the stats file on level 1:

    $CCACHE -M 5 >/dev/null
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    result_file=$(find "$CCACHE_DIR" -name '*R')
    level_2_dir=$(dirname "$result_file")
    level_1_dir=$(dirname $(dirname "$result_file"))
    expect_perm test.o -rw-r--r--
    expect_perm test.d -rw-r--r--
    expect_perm "$CCACHE_CONFIGPATH" -rw-rw-r--
    expect_perm "$CCACHE_DIR" drwxrwxr-x
    expect_perm "$CCACHE_DIR/tmp" drwxrwxr-x
    expect_perm "$level_1_dir" drwxrwxr-x
    expect_perm "$level_1_dir/stats" -rw-rw-r--
    expect_perm "$level_2_dir" drwxrwxr-x
    expect_perm "$result_file" -rw-rw-r--

    rm test.o test.d
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_perm test.o -rw-r--r--
    expect_perm test.d -rw-r--r--

    $CCACHE_COMPILE -o test test.o
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat called_for_link 1
    expect_perm test -rwxr-xr-x

    # A non-cache-miss case which affects the stats file on level 2:

    rm -rf "$CCACHE_DIR"

    $CCACHE_COMPILE --version >/dev/null
    expect_stat no_input_file 1
    stats_file=$(find "$CCACHE_DIR" -name stats)
    level_2_dir=$(dirname "$stats_file")
    level_1_dir=$(dirname $(dirname "$stats_file"))
    expect_perm "$CCACHE_DIR" drwxrwxr-x
    expect_perm "$level_1_dir" drwxrwxr-x
    expect_perm "$level_2_dir" drwxrwxr-x
    expect_perm "$stats_file" -rw-rw-r--

    umask $saved_umask
fi

    # -------------------------------------------------------------------------
    TEST "No object file due to bad prefix"

    cat <<'EOF' >test_no_obj.c
int test_no_obj;
EOF
    cat <<'EOF' >no-object-prefix
#!/bin/sh
# Emulate no object file from the compiler.
EOF
    chmod +x no-object-prefix
    CCACHE_PREFIX=$(pwd)/no-object-prefix $CCACHE_COMPILE -c test_no_obj.c
    expect_stat compiler_produced_no_output 1

    CCACHE_PREFIX=$(pwd)/no-object-prefix $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 0
    expect_stat compiler_produced_no_output 2

    # -------------------------------------------------------------------------
    TEST "-fsyntax-only"

    echo existing >test1.o
    $CCACHE_COMPILE -fsyntax-only test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_content test1.o existing

    rm test1.o

    $CCACHE_COMPILE -fsyntax-only test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_missing test1.o

    # -------------------------------------------------------------------------
    TEST "-fsyntax-only /dev/null"

    echo existing >null.o
    $CCACHE_COMPILE -fsyntax-only -x c /dev/null
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_content null.o existing

    rm null.o

    $CCACHE_COMPILE -fsyntax-only -x c /dev/null
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_missing null.o

    # -------------------------------------------------------------------------
    TEST "No object file due to -fsyntax-only"

    echo '#warning This triggers a compiler warning' >stderr.c

    $COMPILER -Wall stderr.c -fsyntax-only 2>reference_stderr.txt

    expect_contains reference_stderr.txt "This triggers a compiler warning"

    $CCACHE_COMPILE -Wall stderr.c -fsyntax-only 2>stderr.txt
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_stderr.txt stderr.txt

    # Intentionally compiling with "-c" here but not above.
    $CCACHE_COMPILE -Wall -c stderr.c -fsyntax-only 2>stderr.txt
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_equal_content reference_stderr.txt stderr.txt

    # -------------------------------------------------------------------------
    TEST "Empty object file"

    cat <<'EOF' >test_empty_obj.c
int test_empty_obj;
EOF
    cat <<'EOF' >empty-object-prefix
#!/bin/sh
# Emulate empty object file from the compiler.
touch test_empty_obj.o
EOF
    chmod +x empty-object-prefix
    CCACHE_PREFIX=`pwd`/empty-object-prefix $CCACHE_COMPILE -c test_empty_obj.c
    expect_stat compiler_produced_empty_output 1

    # -------------------------------------------------------------------------
    TEST "Output to /dev/null"

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c test1.c -o /dev/null
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Failure to write output file"

    mkdir dir

    $CCACHE_COMPILE -c test1.c -o dir/test1.o
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat bad_output_file 0

    rm dir/test1.o
    chmod a-w dir

    $CCACHE_COMPILE -c test1.c -o dir/test1.o 2>/dev/null
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat bad_output_file 1

    # -------------------------------------------------------------------------
    TEST "Caching stderr"

    cat <<EOF >stderr.c
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF
    $COMPILER -c -Wall -W -c stderr.c 2>reference_stderr.txt
    $CCACHE_COMPILE -Wall -W -c stderr.c 2>stderr.txt
    expect_equal_content reference_stderr.txt stderr.txt

    # -------------------------------------------------------------------------
    TEST "Merging stderr"

    cat >compiler.sh <<EOF
#!/bin/sh
if [ \$1 = -E ]; then
    echo preprocessed
    printf "[%s]" cpp_stderr >&2
else
    echo object >test1.o
    printf "[%s]" cc_stderr >&2
fi
EOF
    chmod +x compiler.sh

    unset CCACHE_NOCPP2
    stderr=$($CCACHE ./compiler.sh -c test1.c 2>stderr)
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_content stderr "[cc_stderr]"

    stderr=$(CCACHE_NOCPP2=1 $CCACHE ./compiler.sh -c test1.c 2>stderr)
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    expect_content stderr "[cpp_stderr][cc_stderr]"

    # -------------------------------------------------------------------------
    TEST "Stderr and dependency file"

    cat <<EOF >test.c
#warning Foo
EOF
    $COMPILER -c test.c -MMD 2>reference.stderr
    mv test.d reference.d

    $CCACHE_COMPILE -c test.c -MMD 2>test.stderr
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_equal_content reference.stderr test.stderr
    expect_equal_content reference.d test.d

    $CCACHE_COMPILE -c test.c -MMD 2>test.stderr
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_equal_content reference.stderr test.stderr
    expect_equal_content reference.d test.d

    # -------------------------------------------------------------------------
    TEST "Caching stdout and stderr"

    cat >compiler.sh <<EOF
#!/bin/sh
if [ \$1 = -E ] || ! echo "\$*" | grep -q '\.i\$'; then
    printf "cpp_err|" >&2
fi
if [ \$1 != -E ]; then
    printf "cc_err|" >&2
    printf "cc_out|"
fi
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    $CCACHE ./compiler.sh -c test1.c >stdout 2>stderr
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_content stdout "cc_out|"
    expect_content stderr "cpp_err|cc_err|"

    $CCACHE ./compiler.sh -c test1.c >stdout 2>stderr
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_content stdout "cc_out|"
    expect_content stderr "cpp_err|cc_err|"

    # -------------------------------------------------------------------------
    TEST "--zero-stats"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    $CCACHE -z >/dev/null
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat files_in_cache 1

    # -------------------------------------------------------------------------
    TEST "--clear"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1

    $CCACHE -C >/dev/null
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 0

    # -------------------------------------------------------------------------
    TEST "-P -c"

    $CCACHE_COMPILE -P -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -P -c test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-P -E"

    $CCACHE_COMPILE -P -E test1.c >/dev/null
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat called_for_preprocessing 1

    # -------------------------------------------------------------------------
    if $COMPILER -c -Wa,-a test1.c >&/dev/null; then
        TEST "-Wa,-a"

        $CCACHE_COMPILE -c -Wa,-a test1.c >first.lst
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1

        $CCACHE_COMPILE -c -Wa,-a test1.c >second.lst
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1
        expect_equal_content first.lst second.lst
    fi

    # -------------------------------------------------------------------------
    if $COMPILER -c -Wa,-a=test1.lst test1.c >&/dev/null; then
        TEST "-Wa,-a=file"

        $CCACHE_COMPILE -c -Wa,-a=test1.lst test1.c
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 0
        expect_stat unsupported_compiler_option 1
    fi

    # -------------------------------------------------------------------------
    TEST "-Wp,-P"

    $CCACHE_COMPILE -c -Wp,-P test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -Wp,-P test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "-Wp,-P,-DFOO"

    # Check that -P disables ccache when used in combination with other -Wp,
    # options. (-P removes preprocessor information in such a way that the
    # object file from compiling the preprocessed file will not be equal to the
    # object file produced when compiling without ccache.)

    $CCACHE_COMPILE -c -Wp,-P,-DFOO test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_compiler_option 1

    $CCACHE_COMPILE -c -Wp,-DFOO,-P test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_compiler_option 2

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -c -DFOO test1.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Handling of compiler-only arguments"

    cat >compiler.sh <<EOF
#!/bin/sh
printf "(%s)" "\$*" >>compiler.args
[ \$1 = -E ] && echo test || echo test >test1.o
EOF
    chmod +x compiler.sh
    backdate compiler.sh

    $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    if [ -z "$CCACHE_NOCPP2" ]; then
        expect_content_pattern compiler.args "(-E -o * test1.c)(-c -o test1.o test1.c)"
    fi
    rm compiler.args

    $CCACHE ./compiler.sh -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 1
    expect_content_pattern compiler.args "(-E -o * test1.c)"
    rm compiler.args

    # Even though -Werror is not passed to the preprocessor, it should be part
    # of the hash, so we expect a cache miss:
    $CCACHE ./compiler.sh -c -Werror -rdynamic test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 2
    if [ -z "$CCACHE_NOCPP2" ]; then
        expect_content_pattern compiler.args "(-E -o * test1.c)(-Werror -rdynamic -c -o test1.o test1.c)"
    fi
    rm compiler.args

    # -------------------------------------------------------------------------

    for src in test1.c build/test1.c; do
        for obj in test1.o build/test1.o; do
            TEST "Dependency file content, $src -o $obj"
            mkdir build
            cp test1.c build
            $CCACHE_COMPILE -c -MMD $src -o $obj
            dep=$(echo $obj | sed 's/\.o$/.d/')
            expect_content $dep "$obj: $src"
        done
    done

    # -------------------------------------------------------------------------
    TEST "Buggy GCC 6 cpp"

    cat >buggy-cpp <<EOF
#!/bin/sh
if echo "\$*" | grep -- -D >/dev/null; then
  $COMPILER "\$@"
else
  # Mistreat the preprocessor output in the same way as GCC 6 does.
  $COMPILER "\$@" |
    sed -e '/^# 1 "<command-line>"\$/ a\\
# 31 "<command-line>"' \\
        -e 's/^# 1 "<command-line>" 2\$/# 32 "<command-line>" 2/'
fi
exit 0
EOF
    cat <<'EOF' >file.c
int foo;
EOF
    chmod +x buggy-cpp

    $CCACHE ./buggy-cpp -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE ./buggy-cpp -DNOT_AFFECTING=1 -c file.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS; then
    TEST ".incbin"

    touch empty.bin

    cat <<EOF >incbin.c
__asm__(".incbin \"empty.bin\"");
EOF

    $CCACHE_COMPILE -c incbin.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_code_directive 1

    cat <<EOF >incbin.c
__asm__(".incbin" " \"empty.bin\"");
EOF

    $CCACHE_COMPILE -c incbin.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_code_directive 2

    cat <<EOF >incbin.s
.incbin "empty.bin";
EOF

    $CCACHE_COMPILE -c incbin.s
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat unsupported_code_directive 3

    cat <<EOF >incbin.cpp
      struct A {
        void incbin() const {}
      };
      void f()
      {
        A a;
        a.incbin();
      }
EOF
    if $CCACHE_COMPILE -x c++ -c incbin.cpp 2>/dev/null; then
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1
        expect_stat unsupported_code_directive 3
    fi
fi

    # -------------------------------------------------------------------------
if ! $HOST_OS_WINDOWS; then
    TEST "UNCACHED_ERR_FD set when executing preprocessor and compiler"

    cat >compiler.sh <<'EOF'
#!/bin/sh
if [ "$1" = "-E" ]; then
    echo preprocessed
    printf ${N}Pu >&$UNCACHED_ERR_FD
else
    echo compiled >test1.o
    printf ${N}Cc >&2
    printf ${N}Cu >&$UNCACHED_ERR_FD
fi
EOF
    chmod +x compiler.sh

    N=1 $CCACHE ./compiler.sh -c test1.c 2>stderr.txt
    stderr=$(cat stderr.txt)
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    if [ "$stderr" != "1Pu1Cu1Cc" ]; then
        test_failed "Unexpected stderr: $stderr != 1Pu1Cu1Cc"
    fi

    N=2 $CCACHE ./compiler.sh -c test1.c 2>stderr.txt
    stderr=$(cat stderr.txt)
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    if [ "$stderr" != "2Pu1Cc" ]; then
        test_failed "Unexpected stderr: $stderr != 2Pu1Cc"
    fi
fi

if ! $HOST_OS_WINDOWS; then
    TEST "UNCACHED_ERR_FD not set when falling back to the original command"
    # -------------------------------------------------------------------------

    $CCACHE bash -c 'echo $UNCACHED_ERR_FD' >uncached_err_fd.txt
    expect_content uncached_err_fd.txt ""
fi

    # -------------------------------------------------------------------------
    TEST "Invalid boolean environment configuration options"

    for invalid_val in 0 false FALSE disable DISABLE no NO; do
        CCACHE_DISABLE=$invalid_val $CCACHE $COMPILER --version >&/dev/null
        if [ $? -eq 0 ] ; then
            test_failed "boolean env var '$invalid_val' should be rejected"
        fi
        CCACHE_NODISABLE=$invalid_val $CCACHE $COMPILER --version >&/dev/null
        if [ $? -eq 0 ] ; then
            test_failed "boolean env var '$invalid_val' should be rejected"
        fi
    done

    # -------------------------------------------------------------------------
    TEST "--hash-file"

    $CCACHE --hash-file /dev/null > hash.out
    printf "a" | $CCACHE --hash-file - >> hash.out

    hash_0='af1396svbud1kqg40jfa6reciicrpcisi'
    hash_1='17765vetiqd4ae95qpbhfb1ut8gj42r6m'

    if grep "$hash_0" hash.out >/dev/null 2>&1 && \
       grep "$hash_1" hash.out >/dev/null 2>&1; then
        : OK
    else
        test_failed "Unexpected output of --hash-file"
    fi
}

# =============================================================================

SUITE_base_SETUP() {
    generate_code 1 test1.c
}

SUITE_base() {
    base_tests
}
