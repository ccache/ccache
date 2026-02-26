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

    # -------------------------------------------------------------------------
    TEST "Directory-specific config file in CWD"

    unset CCACHE_CONFIGPATH
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    export CCACHE_CEILING_DIRS=$ABS_TESTDIR

    echo "namespace = test_ns" > ccache.conf
    $CCACHE --show-config >config.txt

    # Origin is the file path, so check for the file name and value.
    expect_contains config.txt "ccache.conf) namespace = test_ns"

    # -------------------------------------------------------------------------
    TEST "compiler_check non-command value allowed in directory-specific config"

    unset CCACHE_CONFIGPATH
    unset CCACHE_SAFE_DIRS
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    export CCACHE_CEILING_DIRS=$ABS_TESTDIR

    echo "compiler_check = none" > ccache.conf
    $CCACHE --show-config >config.txt
    expect_contains config.txt "ccache.conf) compiler_check = none"

    # -------------------------------------------------------------------------
    TEST "compiler_check command value rejected in directory-specific config"

    unset CCACHE_CONFIGPATH
    unset CCACHE_SAFE_DIRS
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    export CCACHE_CEILING_DIRS=$ABS_TESTDIR

    echo "compiler_check = %compiler% --version" > ccache.conf
    $CCACHE --show-config >config.txt 2>config_err.txt || true
    expect_contains config_err.txt "compiler_check"
    expect_contains config_err.txt "unsafe"

    # -------------------------------------------------------------------------
    TEST "Directory-specific config file in parent directory"

    unset CCACHE_CONFIGPATH
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    export CCACHE_CEILING_DIRS=$ABS_TESTDIR

    echo "namespace = parent_ns" > ccache.conf
    mkdir subdir
    cd subdir
    $CCACHE --show-config >config.txt

    expect_contains config.txt "ccache.conf) namespace = parent_ns"

    cd ..

    # -------------------------------------------------------------------------
    TEST "Directory-specific config file stopped by ceiling dir"

    unset CCACHE_CONFIGPATH
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    # Use pwd -P to get the real (symlink-resolved) path, which matches what
    # getcwd() returns inside ccache.
    export CCACHE_CEILING_DIRS=$(pwd -P)

    echo "namespace = ceiling_ns" > ccache.conf
    $CCACHE --show-config >config.txt

    expect_not_contains config.txt "namespace = ceiling_ns"

    # -------------------------------------------------------------------------
    TEST "Directory-specific config file stopped by ceiling marker"

    unset CCACHE_CONFIGPATH
    export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
    export CCACHE_CEILING_DIRS=$ABS_TESTDIR

    # Layout:
    #   run/
    #     ccache.conf  <- should NOT be found (above the marker)
    #     markerdir/
    #       .git       <- marker stops search here
    #       inner/     <- start ccache from here
    echo "namespace = marker_ns" > ccache.conf
    mkdir -p markerdir/inner
    touch markerdir/.git

    cd markerdir/inner
    $CCACHE --show-config >config.txt

    # Search goes: inner → markerdir (.git found, no ccache.conf) → stop.
    # run/ccache.conf is never reached.
    expect_not_contains config.txt "namespace = marker_ns"

    # -------------------------------------------------------------------------
    # Note: the "not owned by the effective user" error path is not tested here
    # because creating a file owned by another user requires root.

    if ! $HOST_OS_WINDOWS; then
        TEST "Directory-specific config file that is world-writable is an error"

        unset CCACHE_CONFIGPATH
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR

        echo "namespace = bad_ns" > ccache.conf
        chmod o+w ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt || true
        expect_contains config_err.txt "world-writable"
        chmod o-w ccache.conf

        # ---------------------------------------------------------------------
        TEST "Unsafe option in directory-specific config is rejected by default"

        unset CCACHE_CONFIGPATH
        unset CCACHE_SAFE_DIRS
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR

        echo "umask = 077" > ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt || true
        expect_contains config_err.txt "unsafe"
        expect_contains config_err.txt "safe_dirs"

        # ---------------------------------------------------------------------
        TEST "Unsafe option in directory-specific config not allowed under safe_dirs without /*"

        unset CCACHE_CONFIGPATH
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR

        mkdir -p safe_parent/safe_child
        cd safe_parent/safe_child

        export CCACHE_SAFE_DIRS=$(dirname "$(pwd -P)")

        echo "umask = 077" > ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt || true
        expect_contains config_err.txt "unsafe"
        expect_contains config_err.txt "safe_dirs"

        # ---------------------------------------------------------------------
        TEST "Unsafe option in directory-specific config allowed under safe_dirs"

        unset CCACHE_CONFIGPATH
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR
        export CCACHE_SAFE_DIRS=$(dirname "$(pwd -P)")'/*'

        echo "umask = 077" > ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt
        expect_contains config.txt "ccache.conf) umask = 077"

        # ---------------------------------------------------------------------
        TEST "Unsafe option in directory-specific config allowed under safe_dirs=*"

        unset CCACHE_CONFIGPATH
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR
        export CCACHE_SAFE_DIRS='*'

        echo "umask = 077" > ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt
        expect_contains config.txt "ccache.conf) umask = 077"

        # ---------------------------------------------------------------------
        TEST "safe_dirs not allowed in directory-specific config"

        unset CCACHE_CONFIGPATH
        unset CCACHE_SAFE_DIRS
        export CCACHE_CONFIGPATH2=$CCACHE_DIR/ccache.conf
        export CCACHE_CEILING_DIRS=$ABS_TESTDIR

        echo "safe_dirs = /tmp" > ccache.conf
        $CCACHE --show-config >config.txt 2>config_err.txt || true
        expect_contains config_err.txt "not allowed"
    fi
}
