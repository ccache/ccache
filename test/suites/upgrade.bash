SUITE_upgrade() {
    TEST "Keep maxfiles and maxsize settings"

    rm -f $CCACHE_CONFIGPATH
    mkdir -p $CCACHE_DIR/0
    echo "0 0 0 0 0 0 0 0 0 0 0 0 0 2000 131072" >$CCACHE_DIR/0/stats
    expect_stat 'max files' 32000
    expect_stat 'max cache size' '2.1 GB'
}
