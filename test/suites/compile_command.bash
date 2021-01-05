compiler_supports_MJ() {
    touch mjprobe.c
    $REAL_COMPILER -c mjprobe.c -MJ mjprobe.json.part 2> /dev/null
    ret=$?
    rm mjprobe.c
    return $ret
}

SUITE_compile_command_SETUP() {
    generate_code 1 test.c
}

SUITE_compile_command() {
    if compiler_supports_MJ; then
        TEST "requires sloppyness to be set"

        $CCACHE_COMPILE -c test.c -MJtest.json.part
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 0

        expect_exists "test.json.part"
        rm test.json.part

        $CCACHE_COMPILE -c test.c -MJtest.json.part
        expect_stat 'cache hit (direct)' 0
        expect_stat 'cache hit (preprocessed)' 0
        expect_stat 'cache miss' 0

        expect_exists "test.json.part"
    fi

    # -------------------------------------------------------------------------
    TEST "compile command sloppiness enabled (direct)"
    unset CCACHE_NODIRECT

    # CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command" $CCACHE_COMPILE ... # NOT working...

    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"
    $CCACHE_COMPILE -c test.c -MJtest.json.part

    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    cp test.json.part test_1.json.part

    $CCACHE_COMPILE -c test.c -MJ test.json.part
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_equal_content test.json.part test_1.json.part

    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS"
    rm test.json.part

    $CCACHE_COMPILE -c test.c --ccache-MJ test.json.part
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_equal_content test.json.part test_1.json.part

    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS"
    rm test.json.part

    $CCACHE_COMPILE -c test.c --ccache-auto-MJ
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_equal_content test.ccmd test_1.json.part

    # none of these flags are hashed, so this is also a cache hit:
    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 4
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_equal_content test.ccmd test_1.json.part

    # -------------------------------------------------------------------------
    TEST "compile command sloppiness enabled (preprocessed)"
    # unset CCACHE_NODIRECT
    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"

    $CCACHE_COMPILE -c test.c -MJtest.json.part

    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    cp test.json.part test_1.json.part

    $CCACHE_COMPILE -c test.c -MJ test.json.part
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    expect_equal_content test.json.part test_1.json.part

    # -------------------------------------------------------------------------
    TEST "compile command test argument splitting and removal"
    unset CCACHE_NODIRECT

    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"
    $CCACHE_COMPILE -xc++ -c test.c -Darg1 -D arg2 -MMD -MF out.d -Ii1 -I i2 \
                    -isystemi3 -isystemi4 -MJtest.json.part

    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"
    $CCACHE_COMPILE -xc++ -c test.c -Darg1 -D arg2 -MMD -MF out.d -Ii1 -I i2 \
                    -isystemi3 -isystemi4 -MJtest.json.part

    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_contains test.json.part '"arg1"'
    expect_contains test.json.part '"arg2"'
    expect_contains test.json.part '"i1"'
    expect_contains test.json.part '"i2"'
    expect_contains test.json.part '"i3"'
    expect_contains test.json.part '"i4"'
    grep -q out.d test.json.part && test_failed 'output should not contain "out.d"'

    # -------------------------------------------------------------------------
    TEST "changed dir"
    unset CCACHE_NODIRECT
    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"

    $CCACHE_COMPILE -c test.c -MJtest.json.part -g -fdebug-prefix-map="$PWD=/abc"
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    $CCACHE_COMPILE -c test.c -MJtest.json.part -g -fdebug-prefix-map="$PWD=/abc"
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    mkdir -p subdir && cd subdir && cp ../test.c .

    $CCACHE_COMPILE -c test.c -MJtest.json.part -g -fdebug-prefix-map="$PWD=/abc"
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    expect_different_content test.json.part ../test.json.part

    $CCACHE_COMPILE -c test.c -MJtest.json.part -g -fdebug-prefix-map="$PWD=/ABCDEF"
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
#    TEST "on compiler error no .ccmd file is generated"
#    unset CCACHE_NODIRECT
#    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"

#    $CCACHE_COMPILE -c test.c -MJtest.json.part -QERROR_ARG 2> /dev/null
#    [ "x$?" = "x0" ] && test_failed "compiler call should have failed"
#    expect_file_missing test.json.part

#    # -------------------------------------------------------------------------
#    TEST "on other TOO_HARD argument no error no .ccmd file is generated"
#    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"

#    $CCACHE_COMPILE -c test.c -MJtest.json.part -save-temps=cwd
#    [ "x$?" = "x0" ] || test_failed "compiler call should succeed"
#    expect_file_missing test.json.part

    # -------------------------------------------------------------------------
    TEST "on other TOO_HARD argument no error a .ccmd file IS generated"
    export CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS,compile_command"

    $CCACHE_COMPILE -c test.c -MJtest.json.part -save-temps=cwd
    [ "x$?" = "x0" ] || test_failed "compiler call should succeed"
    expect_contains test.json.part "-save-temps=cwd "

    # -------------------------------------------------------------------------
    TEST "CCACHE_DISABLE?"
    # unset CCACHE_NODIRECT
    # export CCACHE_DISABLE=1
    # $CCACHE_COMPILE -c test.c -MJtest.json.part
}
