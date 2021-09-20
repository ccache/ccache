SUITE_config() {
    # -------------------------------------------------------------------------
    TEST "Environment origin"

    export CCACHE_MAXSIZE="40"

    $CCACHE --max-size "75" >/dev/null
    $CCACHE --show-config >config.txt

    expect_contains config.txt "(environment) max_size = 40"
}
