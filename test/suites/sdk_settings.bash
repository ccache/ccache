SUITE_sdk_settings_PROBE() {
    mkdir probe_sdk
    echo 'int test;' >probe.c
    if ! $COMPILER -isysroot probe_sdk -c probe.c 2>/dev/null; then
        echo "-isysroot not supported by compiler"
    fi
}

SUITE_sdk_settings_SETUP() {
    unset CCACHE_NODIRECT
    unset SDKROOT
    mkdir sdk other_sdk
    echo '{"Version":"15.0"}' >sdk/SDKSettings.json
    echo '{"Version":"14.0"}' >other_sdk/SDKSettings.json
    echo 'int test;' >test.c
}

SUITE_sdk_settings() {
    # No SDK headers are needed: the version metadata alone must affect all three
    # cache modes, even when the preprocessed source stays identical.
    for mode in direct preprocessor depend; do
        for option in separate concatenated absolute environment; do
            TEST "$mode mode, $option SDK selection"
            if [ "$mode" = preprocessor ]; then
                export CCACHE_NODIRECT=1
                hit=preprocessed_cache_hit
            else
                hit=direct_cache_hit
                if [ "$mode" = depend ]; then
                    export CCACHE_DEPEND=1
                fi
            fi
            case $option in
                separate) args="-isysroot sdk" ;;
                concatenated) args="-isysrootsdk" ;;
                absolute) args="-isysroot$PWD/sdk" ;;
                environment) args=""; export SDKROOT=$PWD/sdk ;;
            esac

            $CCACHE_COMPILE $args -MD -c test.c
            expect_stat cache_miss 1
            $CCACHE_COMPILE $args -MD -c test.c
            expect_stat $hit 1

            echo '{"Version":"15.1"}' >sdk/SDKSettings.json
            $CCACHE_COMPILE $args -MD -c test.c
            expect_stat cache_miss 2
            expect_stat $hit 1
            $CCACHE_COMPILE $args -MD -c test.c
            expect_stat $hit 2
        done
    done

    TEST "Explicit -isysroot takes precedence over SDKROOT"
    export SDKROOT=$PWD/other_sdk
    $CCACHE_COMPILE -isysroot sdk -c test.c
    echo '{"Version":"14.1"}' >other_sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot sdk -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    echo '{"Version":"15.1"}' >sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot sdk -c test.c
    expect_stat cache_miss 2

    TEST "Last -isysroot wins"
    $CCACHE_COMPILE -isysroot other_sdk -isysrootsdk -c test.c
    echo '{"Version":"14.1"}' >other_sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot other_sdk -isysrootsdk -c test.c
    expect_stat direct_cache_hit 1
    echo '{"Version":"15.1"}' >sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot other_sdk -isysrootsdk -c test.c
    expect_stat cache_miss 2

    TEST "SDK symlink changes at the same path"
    ln -s sdk selected_sdk
    $CCACHE_COMPILE -isysroot selected_sdk -c test.c
    $CCACHE_COMPILE -isysroot selected_sdk -c test.c
    expect_stat direct_cache_hit 1
    rm selected_sdk
    ln -s other_sdk selected_sdk
    $CCACHE_COMPILE -isysroot selected_sdk -c test.c
    expect_stat cache_miss 2

    TEST "SDKSettings.json is optional, and adding or removing it invalidates"
    rm sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot sdk -c test.c
    $CCACHE_COMPILE -isysroot sdk -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    echo '{"Version":"15.0"}' >sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot sdk -c test.c
    expect_stat cache_miss 2
    rm sdk/SDKSettings.json
    $CCACHE_COMPILE -isysroot sdk -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
}
