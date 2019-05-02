base_tests() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    $REAL_COMPILER -c -o reference_test1.o test1.c

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "Debug option"

    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $REAL_COMPILER -c -o reference_test1.o test1.c -g
    expect_equal_object_files reference_test1.o reference_test1.o

    # -------------------------------------------------------------------------
    TEST "Output option"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c -o foo.o
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $REAL_COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o foo.o

    # -------------------------------------------------------------------------
    TEST "Output option without space"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c -odir
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c -optf
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    $REAL_COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o dir
    expect_equal_object_files reference_test1.o ptf

    # -------------------------------------------------------------------------
    TEST "Called for link"

    $CCACHE_COMPILE test1.c -o test 2>/dev/null
    expect_stat 'called for link' 1

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE test1.o -o test 2>/dev/null
    expect_stat 'called for link' 2

    # -------------------------------------------------------------------------
    TEST "No input file"

    $CCACHE_COMPILE -c foo.c 2>/dev/null
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "Called for preprocessing"

    $CCACHE_COMPILE -E -c test1.c >/dev/null 2>&1
    expect_stat 'called for preprocessing' 1

    # -------------------------------------------------------------------------
    TEST "Multiple source files"

    touch test2.c
    $CCACHE_COMPILE -c test1.c test2.c
    expect_stat 'multiple source files' 1

    # -------------------------------------------------------------------------
    TEST "Couldn't find the compiler"

    $CCACHE blahblah -c test1.c 2>/dev/null
    expect_stat "couldn't find the compiler" 1

    # -------------------------------------------------------------------------
    TEST "Bad compiler arguments"

    $CCACHE_COMPILE -c test1.c -I 2>/dev/null
    expect_stat 'bad compiler arguments' 1

    # -------------------------------------------------------------------------
    TEST "Unsupported source language"

    ln -f test1.c test1.ccc
    $CCACHE_COMPILE -c test1.ccc 2>/dev/null
    expect_stat 'unsupported source language' 1

    # -------------------------------------------------------------------------
    TEST "Unsupported compiler option"

    $CCACHE_COMPILE -M foo -c test1.c >/dev/null 2>&1
    expect_stat 'unsupported compiler option' 1

    # -------------------------------------------------------------------------
    TEST "Compiler produced stdout"

    $CCACHE echo foo -c test1.c >/dev/null
    expect_stat 'compiler produced stdout' 1

    # -------------------------------------------------------------------------
    TEST "Output to directory"

    mkdir testd
    $CCACHE_COMPILE -o testd -c test1.c >/dev/null 2>&1
    rmdir testd >/dev/null 2>&1
    expect_stat 'could not write to output file' 1

    # -------------------------------------------------------------------------
    TEST "Output to file in nonexistent directory"

    mkdir out

    $CCACHE_COMPILE -c test1.c -o out/foo.o
    expect_stat 'could not write to output file' ""
    expect_stat 'cache miss' 1

    rm -rf out

    $CCACHE_COMPILE -c test1.c -o out/foo.o 2>/dev/null
    expect_stat 'could not write to output file' 1
    expect_stat 'cache miss' 1
    expect_file_missing out/foo.o

    # -------------------------------------------------------------------------
    TEST "No input file"

    $CCACHE_COMPILE -c -O2 2>/dev/null
    expect_stat 'no input file' 1

    # -------------------------------------------------------------------------
    TEST "No file extension"

    mkdir src
    touch src/foo

    $CCACHE_COMPILE -x c -c src/foo
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists foo.o
    rm foo.o

    $CCACHE_COMPILE -x c -c src/foo
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_exists foo.o
    rm foo.o

    rm -rf src

    # -------------------------------------------------------------------------
    TEST "Source file ending with dot"

    mkdir src
    touch src/foo.

    $CCACHE_COMPILE -x c -c src/foo.
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists foo.o
    rm foo.o

    $CCACHE_COMPILE -x c -c src/foo.
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_exists foo.o
    rm foo.o

    rm -rf src

    # -------------------------------------------------------------------------
    TEST "Multiple file extensions"

    mkdir src
    touch src/foo.c.c

    $CCACHE_COMPILE -c src/foo.c.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists foo.c.o
    rm foo.c.o

    $CCACHE_COMPILE -c src/foo.c.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_exists foo.c.o
    rm foo.c.o

    rm -rf src

    # -------------------------------------------------------------------------
    TEST "LANG"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2

    LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2
    expect_stat 'files in cache' 2

    # -------------------------------------------------------------------------
    TEST "LANG with sloppiness"

    CCACHE_SLOPPINESS=locale LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    CCACHE_SLOPPINESS=locale LANG=foo $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    CCACHE_SLOPPINESS=locale LANG=bar $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_DISABLE"

    CCACHE_DISABLE=1 $CCACHE_COMPILE -c test1.c 2>/dev/null
    if [ -d $CCACHE_DIR ]; then
        test_failed "$CCACHE_DIR created despite CCACHE_DISABLE being set"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMMENTS"

    $REAL_COMPILER -c -o reference_test1.o test1.c

    mv test1.c test1-saved.c
    echo '// initial comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    echo '// different comment' >test1.c
    cat test1-saved.c >>test1.c
    CCACHE_COMMENTS=1 $CCACHE_COMPILE -c test1.c
    mv test1-saved.c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $REAL_COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOSTATS"

    CCACHE_NOSTATS=1 $CCACHE_COMPILE -c test1.c -O -O
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $REAL_COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "Directory is hashed if using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
    $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if not using -g"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Directory is not hashed if using -g -g0"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    $CCACHE_COMPILE -c test1.c -g -g0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_NOHASHDIR"

    mkdir dir1 dir2
    cp test1.c dir1
    cp test1.c dir2

    cd dir1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cd ../dir2
    CCACHE_NOHASHDIR=1 $CCACHE_COMPILE -c test1.c -g
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_UNIFY"

    echo '// a silly comment' >>test1.c
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo '// another silly comment' >>test1.c
    CCACHE_UNIFY=1 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $REAL_COMPILER -c -o reference_test1.o test1.c
    expect_equal_object_files reference_test1.o test1.o

    # -------------------------------------------------------------------------
    TEST "CCACHE_NLEVELS"

    CCACHE_NLEVELS=4 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    CCACHE_NLEVELS=4 $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    # Directories in $CCACHE_DIR:
    # - .
    # - tmp
    # - a
    # - a/b
    # - a/b/c
    # - a/b/c/d
    actual_dirs=$(find $CCACHE_DIR -type d | wc -l)
    expected_dirs=6
    if [ $actual_dirs -ne $expected_dirs ]; then
        test_failed "Expected $expected_dirs directories, found $actual_dirs"
    fi

    # -------------------------------------------------------------------------
    TEST "CCACHE_EXTRAFILES"

    echo "a" >a
    echo "b" >b

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    echo b2 >b

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 3

    CCACHE_EXTRAFILES="a${PATH_DELIM}b" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 3
    expect_stat 'cache miss' 3

    CCACHE_EXTRAFILES="doesntexist" $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 3
    expect_stat 'cache miss' 3
    expect_stat 'error hashing extra file' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_PREFIX"

    cat <<'EOF' >prefix-a
#!/bin/sh
echo a >>prefix.result
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
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    PATH=.:$PATH CCACHE_PREFIX="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    rm -f prefix.result
    PATH=.:$PATH CCACHE_PREFIX_CPP="prefix-a prefix-b" $CCACHE_COMPILE -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1
    expect_file_content prefix.result "a
b"

    # -------------------------------------------------------------------------
    TEST "Files in cache"

    for i in $(seq 32); do
        generate_code $i test$i.c
        $CCACHE_COMPILE -c test$i.c
    done
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 32
    expect_stat 'files in cache' 32

    # -------------------------------------------------------------------------
    TEST "Called for preprocessing"

    $CCACHE_COMPILE -c test1.c -E >test1.i
    expect_stat 'called for preprocessing' 1

    # -------------------------------------------------------------------------
    TEST "Direct .i compile"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $REAL_COMPILER -c test1.c -E >test1.i
    $CCACHE_COMPILE -c test1.i
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x c"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x c -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-xc"

    ln -f test1.c test1.ccc

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -xc -c test1.ccc
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x none"

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x assembler -x none -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-x unknown"

    $CCACHE_COMPILE -x unknown -c test1.c 2>/dev/null
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported source language' 1

    # -------------------------------------------------------------------------
    TEST "-x c -c /dev/null"

    $CCACHE_COMPILE -x c -c /dev/null -o null.o 2>/dev/null
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -x c -c /dev/null -o null.o 2>/dev/null
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-D not hashed"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -DNOT_AFFECTING=1 -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "-S"

    $CCACHE_COMPILE -S test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -S test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    $CCACHE_COMPILE -c test1.s
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

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
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
# A comment
EOF
    chmod +x compiler.sh
    backdate compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    sed_in_place 's/comment/yoghurt/' compiler.sh # Don't change the size
    chmod +x compiler.sh
    backdate compiler.sh # Don't change the timestamp

    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    touch compiler.sh
    CCACHE_COMPILERCHECK=mtime $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=content"

    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    echo "# Compiler upgrade" >>compiler.sh

    CCACHE_COMPILERCHECK=content $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=none"

    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK=none $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=string"

    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=string:foo $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_COMPILERCHECK=string:bar $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=command"

    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK='echo %compiler%' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "# Compiler upgrade" >>compiler.sh
    CCACHE_COMPILERCHECK="echo ./compiler.sh" $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    cat <<EOF >foobar.sh
#!/bin/sh
echo foo
echo bar
EOF
    chmod +x foobar.sh
    CCACHE_COMPILERCHECK='./foobar.sh' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    CCACHE_COMPILERCHECK='echo foo; echo bar' $CCACHE ./compiler.sh -c test1.c
    expect_stat 'cache hit (preprocessed)' 2
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "CCACHE_COMPILERCHECK=unknown_command"

    cat >compiler.sh <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
exec $COMPILER "\$@"
EOF
    chmod +x compiler.sh

    CCACHE_COMPILERCHECK="unknown_command" $CCACHE ./compiler.sh -c test1.c 2>/dev/null
    if [ "$?" -eq 0 ]; then
        test_failed "Expected failure running unknown_command to verify compiler but was success"
    fi
    expect_stat 'compiler check failed' 1

    # -------------------------------------------------------------------------
    TEST "CCACHE_RECACHE should remove previous .stderr"

    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_count 0 '*.stderr' $CCACHE_DIR

    obj_file=`find $CCACHE_DIR -name '*.o'`
    stderr_file=`echo $obj_file | sed 's/..$/.stderr/'`
    test -n "$stderr_file" && echo "Warning: foo" >$stderr_file
    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test1.c
    expect_file_count 0 '*.stderr' $CCACHE_DIR

    # -------------------------------------------------------------------------
    TEST "No object file"

    cat <<'EOF' >test_no_obj.c
int test_no_obj;
EOF
    cat <<'EOF' >prefix-remove.sh
#!/bin/sh
"$@"
[ x$2 = x-fcolor-diagnostics ] && shift
[ x$2 = x-fdiagnostics-color ] && shift
[ x$2 = x-std=gnu99 ] && shift
[ x$3 = x-o ] && rm $4
EOF
    chmod +x prefix-remove.sh
    CCACHE_PREFIX=`pwd`/prefix-remove.sh $CCACHE_COMPILE -c test_no_obj.c
    expect_stat 'compiler produced no output' 1

    # -------------------------------------------------------------------------
    TEST "Empty object file"

    cat <<'EOF' >test_empty_obj.c
int test_empty_obj;
EOF
    cat <<'EOF' >prefix-empty.sh
#!/bin/sh
"$@"
[ x$2 = x-fcolor-diagnostics ] && shift
[ x$2 = x-fdiagnostics-color ] && shift
[ x$2 = x-std=gnu99 ] && shift
[ x$3 = x-o ] && cp /dev/null $4
EOF
    chmod +x prefix-empty.sh
    CCACHE_PREFIX=`pwd`/prefix-empty.sh $CCACHE_COMPILE -c test_empty_obj.c
    expect_stat 'compiler produced empty output' 1

    # -------------------------------------------------------------------------
    TEST "Caching stderr"

    cat <<EOF >stderr.c
int stderr(void)
{
  // Trigger warning by having no return statement.
}
EOF
    $REAL_COMPILER -c -Wall -W -c stderr.c 2>reference_stderr.txt
    $CCACHE_COMPILE -Wall -W -c stderr.c 2>stderr.txt
    expect_equal_files reference_stderr.txt stderr.txt

    # -------------------------------------------------------------------------
    TEST "--zero-stats"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE -z >/dev/null
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 1

    # -------------------------------------------------------------------------
    TEST "--clear"

    $CCACHE_COMPILE -c test1.c
    $CCACHE_COMPILE -c test1.c
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 1

    $CCACHE -C >/dev/null
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0

    # -------------------------------------------------------------------------
    TEST "-P"

    # Check that -P disables ccache. (-P removes preprocessor information in
    # such a way that the object file from compiling the preprocessed file will
    # not be equal to the object file produced when compiling without ccache.)

    $CCACHE_COMPILE -c -P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 1

    # -------------------------------------------------------------------------
    TEST "-Wp,-P"

    # Check that -Wp,-P disables ccache. (-P removes preprocessor information
    # in such a way that the object file from compiling the preprocessed file
    # will not be equal to the object file produced when compiling without
    # ccache.)

    $CCACHE_COMPILE -c -Wp,-P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 1

    $CCACHE_COMPILE -c -Wp,-P,-DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 2

    $CCACHE_COMPILE -c -Wp,-DFOO,-P test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported compiler option' 3

    # -------------------------------------------------------------------------
    TEST "-Wp,-D"

    $CCACHE_COMPILE -c -Wp,-DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c -DFOO test1.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Buggy GCC 6 cpp"

    cat >buggy-cpp <<EOF
#!/bin/sh
CCACHE_DISABLE=1 # If $COMPILER happens to be a ccache symlink...
export CCACHE_DISABLE
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
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE ./buggy-cpp -DNOT_AFFECTING=1 -c file.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Symlink to source directory"

    mkdir dir
    cd dir
    mkdir -p d1/d2
    echo '#define A "OK"' >d1/h.h
    cat <<EOF >d1/d2/c.c
#include <stdio.h>
#include "../h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "BUG"' >h.h
    ln -s d1/d2 d3

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/d3/c.c
    $REAL_COMPILER c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi

    # -------------------------------------------------------------------------
    TEST "Symlink to source file"

    mkdir dir
    cd dir
    mkdir d
    echo '#define A "BUG"' >d/h.h
    cat <<EOF >d/c.c
#include <stdio.h>
#include "h.h"
int main() { printf("%s\n", A); }
EOF
    echo '#define A "OK"' >h.h
    ln -s d/c.c c.c

    CCACHE_BASEDIR=/ $CCACHE_COMPILE -c $PWD/c.c
    $REAL_COMPILER c.o -o c
    if [ "$(./c)" != OK ]; then
        test_failed "Incorrect header file used"
    fi

    # -------------------------------------------------------------------------
    TEST ".incbin"

    cat <<EOF >incbin.c
char x[] = ".incbin";
EOF

    $CCACHE_COMPILE -c incbin.c
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'unsupported code directive' 1

    # -------------------------------------------------------------------------
    TEST "UNCACHED_ERR_FD"

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
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    if [ "$stderr" != "1Pu1Cu1Cc" ]; then
        test_failed "Unexpected stderr: $stderr != 1Pu1Cu1Cc"
    fi

    N=2 $CCACHE ./compiler.sh -c test1.c 2>stderr.txt
    stderr=$(cat stderr.txt)
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1
    if [ "$stderr" != "2Pu1Cc" ]; then
        test_failed "Unexpected stderr: $stderr != 2Pu1Cc"
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

    >empty
    $CCACHE --hash-file empty > hash.out
    printf "a" | $CCACHE --hash-file - >> hash.out

    if grep '31d6cfe0d16ae931b73c59d7e0c089c0-0' hash.out >/dev/null 2>&1 && \
       grep 'bde52cb31de33e46245e05fbdbd6fb24-1' hash.out >/dev/null 2>&1; then
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
