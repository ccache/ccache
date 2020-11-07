if $COMPILER_TYPE_GCC; then
    color_diagnostics_enable='-fdiagnostics-color'
    color_diagnostics_disable='-fno-diagnostics-color'
elif $COMPILER_TYPE_CLANG; then
    color_diagnostics_enable='-fcolor-diagnostics'
    color_diagnostics_disable='-fno-color-diagnostics'
fi

SUITE_color_diagnostics_PROBE() {
    if ! script --return --quiet --command true /dev/null </dev/null >/dev/null 2>&1; then
        echo "the script tool is not installed or does not support required options"
        return
    fi

    # Probe that real compiler actually supports colored diagnostics.
    if [[ ! $color_diagnostics_enable || ! $color_diagnostics_disable ]]; then
        echo "compiler $COMPILER does not support colored diagnostics"
    elif ! $REAL_COMPILER $color_diagnostics_enable -E - </dev/null >/dev/null 2>&1; then
        echo "compiler $COMPILER (version: $compiler_version) does not support $color_diagnostics_enable"
    elif ! $REAL_COMPILER $color_diagnostics_disable -E - </dev/null >/dev/null 2>&1; then
        echo "compiler $COMPILER (version: $compiler_version) does not support $color_diagnostics_disable"
    fi
}

SUITE_color_diagnostics_SETUP() {
    if $run_second_cpp; then
        export CCACHE_CPP2=1
    else
        export CCACHE_NOCPP2=1
    fi

    unset GCC_COLORS
    export TERM=vt100
}

color_diagnostics_expect_color() {
    expect_contains "${1:?}" $'\033['
    expect_contains <(fgrep 'previous prototype' "$1") $'\033['
    expect_contains <(fgrep 'from preprocessor' "$1") $'\033['
}

color_diagnostics_expect_no_color() {
    expect_not_contains "${1:?}" $'\033['
}

color_diagnostics_generate_code() {
    generate_code "$@"
    echo '#warning "Warning from preprocessor"' >>"$2"
}

# Heap's permutation algorithm
color_diagnostics_generate_permutations() {
    local -i i
    local -i k="${1:?}-1"
    if ((k)); then
        color_diagnostics_generate_permutations "$k"
        for ((i = 0; i < k; ++i)); do
            if ((k & 1)); then
                local tmp=${A[$i]}
                A[$i]=${A[$k]}
                A[$k]=$tmp
            else
                local tmp=${A[0]}
                A[0]=${A[$k]}
                A[$k]=$tmp
            fi
            color_diagnostics_generate_permutations "$k"
        done
    else
        echo "${A[@]}"
    fi
}

color_diagnostics_run_on_pty() {
    script --return --quiet --command "unset GCC_COLORS; CCACHE_DIR='$CCACHE_DIR' ${2:?}" /dev/null </dev/null >"${1:?}"

    # script returns early on some platforms (leaving a child process living for
    # a short while) and the output may therefore still be pending. Work around
    # this by sleeping until the output file has content.
    retries=0
    while [ ! -s "$1" -a "$retries" -lt 100 ]; do
        sleep 0.1
        retries=$((retries + 1))
    done
}

color_diagnostics_test() {
    # -------------------------------------------------------------------------
    TEST "Colored diagnostics automatically disabled when stderr is not a TTY (run_second_cpp=$run_second_cpp)"

    color_diagnostics_generate_code 1 test1.c
    $CCACHE_COMPILE -Wmissing-prototypes -c -o test1.o test1.c 2>test1.stderr
    color_diagnostics_expect_no_color test1.stderr

    # Check that subsequently running on a TTY generates a cache hit.
    color_diagnostics_run_on_pty test1.output "$CCACHE_COMPILE -Wmissing-prototypes -c -o test1.o test1.c"
    color_diagnostics_expect_color test1.output
    expect_stat 'cache miss' 1
    expect_stat 'cache hit (preprocessed)' 1

    # -------------------------------------------------------------------------
    TEST "Colored diagnostics automatically enabled when stderr is a TTY (run_second_cpp=$run_second_cpp)"

    color_diagnostics_generate_code 1 test1.c
    color_diagnostics_run_on_pty test1.output "$CCACHE_COMPILE -Wmissing-prototypes -c -o test1.o test1.c"
    color_diagnostics_expect_color test1.output

    # Check that subsequently running without a TTY generates a cache hit.
    $CCACHE_COMPILE -Wmissing-prototypes -c -o test1.o test1.c 2>test1.stderr
    color_diagnostics_expect_no_color test1.stderr
    expect_stat 'cache miss' 1
    expect_stat 'cache hit (preprocessed)' 1

    # -------------------------------------------------------------------------
    if $COMPILER_TYPE_GCC; then
        TEST "-fcolor-diagnostics not accepted for GCC"

        generate_code 1 test.c
        if $CCACHE_COMPILE -fcolor-diagnostics -c test.c >&/dev/null; then
            test_failed "-fcolor-diagnostics unexpectedly accepted by GCC"
        fi
    fi

    while read -r case; do
        TEST "Cache object shared across ${case} (run_second_cpp=$run_second_cpp)"

        color_diagnostics_generate_code 1 test1.c
        local each
        for each in ${case}; do
            case $each in
                color,*)
                    local color_flag=$color_diagnostics_enable
                    local color_expect=color
                    ;;
                nocolor,*)
                    local color_flag=$color_diagnostics_disable
                    local color_expect=no_color
                    ;;
            esac
            case $each in
                *,tty)
                    color_diagnostics_run_on_pty test1.output "$CCACHE_COMPILE $color_flag -Wmissing-prototypes -c -o test1.o test1.c"
                    color_diagnostics_expect_$color_expect test1.output
                    ;;
                *,notty)
                    $CCACHE_COMPILE $color_flag -Wmissing-prototypes -c -o test1.o test1.c 2>test1.stderr
                    color_diagnostics_expect_$color_expect test1.stderr
                    ;;
            esac
        done
        expect_stat 'cache miss' 1
        expect_stat 'cache hit (preprocessed)' 3
    done < <(
        A=({color,nocolor},{tty,notty})
        color_diagnostics_generate_permutations "${#A[@]}"
    )
}

SUITE_color_diagnostics() {
    run_second_cpp=true color_diagnostics_test
    run_second_cpp=false color_diagnostics_test
}
