SUITE_upgrade() {
    # -------------------------------------------------------------------------
    TEST "Default cache config/directory without XDG variables"

    unset CCACHE_CONFIGPATH
    unset CCACHE_DIR
    export HOME=/home/user

    if $HOST_OS_APPLE; then
        expected=$HOME/Library/Caches/ccache
    else
        expected=$HOME/.cache/ccache
    fi
    actual=$($CCACHE -s | sed -n 's/^cache directory *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected cache directory $expected, actual $actual"
    fi

    if $HOST_OS_APPLE; then
        expected=$HOME/Library/Preferences/ccache/ccache.conf
    else
        expected=$HOME/.config/ccache/ccache.conf
    fi
    actual=$($CCACHE -s | sed -n 's/^primary config *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected primary config $expected actual $actual"
    fi

    # -------------------------------------------------------------------------
    TEST "Default cache config/directory with XDG variables"

    unset CCACHE_CONFIGPATH
    unset CCACHE_DIR
    export HOME=$PWD
    export XDG_CACHE_HOME=/somewhere/cache
    export XDG_CONFIG_HOME=/elsewhere/config

    expected=$XDG_CACHE_HOME/ccache
    actual=$($CCACHE -s | sed -n 's/^cache directory *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected cache directory $expected, actual $actual"
    fi

    expected=$XDG_CONFIG_HOME/ccache/ccache.conf
    actual=$($CCACHE -s | sed -n 's/^primary config *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected primary config $expected actual $actual"
    fi

    # -------------------------------------------------------------------------
    TEST "Cache config/directory with XDG variables and legacy directory"

    unset CCACHE_CONFIGPATH
    unset CCACHE_DIR
    export HOME=$PWD
    export XDG_CACHE_HOME=/somewhere/cache
    export XDG_CONFIG_HOME=/elsewhere/config
    mkdir $HOME/.ccache

    expected=$HOME/.ccache
    actual=$($CCACHE -s | sed -n 's/^cache directory *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected cache directory $expected, actual $actual"
    fi

    expected=$HOME/.ccache/ccache.conf
    actual=$($CCACHE -s | sed -n 's/^primary config *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected primary config $expected actual $actual"
    fi

    # -------------------------------------------------------------------------
    TEST "Cache config/directory with XDG variables and CCACHE_DIR"

    unset CCACHE_CONFIGPATH
    export CCACHE_DIR=$PWD/test
    export HOME=/home/user
    export XDG_CACHE_HOME=/somewhere/cache
    export XDG_CONFIG_HOME=/elsewhere/config

    expected=$CCACHE_DIR
    actual=$($CCACHE -s | sed -n 's/^cache directory *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected cache directory $expected, actual $actual"
    fi

    expected=$CCACHE_DIR/ccache.conf
    actual=$($CCACHE -s | sed -n 's/^primary config *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected primary config $expected actual $actual"
    fi

    # -------------------------------------------------------------------------
    TEST "Cache config/directory with empty CCACHE_DIR"

    # Empty (but set) CCACHE_DIR means "use defaults" and should thus override
    # cache_dir set in the secondary config.

    unset CCACHE_CONFIGPATH
    export CCACHE_CONFIGPATH2=$PWD/ccache.conf2
    export HOME=/home/user
    export XDG_CACHE_HOME=/somewhere/cache
    export XDG_CONFIG_HOME=/elsewhere/config
    export CCACHE_DIR= # Set but empty
    echo 'cache_dir = /nowhere' > $CCACHE_CONFIGPATH2

    expected=$XDG_CACHE_HOME/ccache
    actual=$($CCACHE -s | sed -n 's/^cache directory *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected cache directory $expected, actual $actual"
    fi

    expected=$XDG_CONFIG_HOME/ccache/ccache.conf
    actual=$($CCACHE -s | sed -n 's/^primary config *//p')
    if [ "$actual" != "$expected" ]; then
        test_failed "expected primary config $expected actual $actual"
    fi
}
