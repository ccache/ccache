SUITE_config() {
    # -------------------------------------------------------------------------
    TEST "Environment origin"

    export CCACHE_MAXSIZE="40"

    $CCACHE --max-size "75" >/dev/null
    $CCACHE --show-config >config.txt

    expect_contains config.txt "(environment) max_size = 40"

    # -------------------------------------------------------------------------
    TEST "Command line origin"

    export CCACHE_DEBUG="1"
    export CCACHE_MAXSIZE="40"

    touch test.c
    $CCACHE debug=true "max_size = 40" $COMPILER -c test.c

    expect_contains test.o.*.ccache-log "(command line) debug = true"
    expect_contains test.o.*.ccache-log "(command line) max_size = 40"
}
