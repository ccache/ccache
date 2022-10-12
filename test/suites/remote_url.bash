SUITE_remote_url_SETUP() {
    generate_code 1 test.c
}

SUITE_remote_url() {
    # -------------------------------------------------------------------------
    TEST "Reject empty url (without config attributes)"

    export CCACHE_REMOTE_STORAGE="|"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "must provide a URL"

    # -------------------------------------------------------------------------
    TEST "Reject empty url (but with config attributes)"

    export CCACHE_REMOTE_STORAGE="|key=value"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "must provide a URL"

    # -------------------------------------------------------------------------
    TEST "Reject invalid url"

    export CCACHE_REMOTE_STORAGE="://qwerty"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "Cannot parse URL"

    # -------------------------------------------------------------------------
if $RUN_WIN_XFAIL; then
    TEST "Reject missing scheme"

    export CCACHE_REMOTE_STORAGE="/qwerty"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "URL scheme must not be empty"

    # -------------------------------------------------------------------------
    TEST "Reject user info defined but no host"

    export CCACHE_REMOTE_STORAGE="http://foo@"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "User info defined, but host is empty"

    # -------------------------------------------------------------------------
    TEST "Reject relative path with colon in first part"

    export CCACHE_REMOTE_STORAGE="file:foo:bar"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log "The first segment of the relative path can't contain ':'"
fi
}
