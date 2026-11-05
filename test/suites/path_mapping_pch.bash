SUITE_path_mapping_pch_PROBE() {
    if ! $COMPILER_TYPE_CLANG || $HOST_OS_WINDOWS; then
        echo "requires Clang and POSIX paths"
        return
    fi
    local root
    root=$(pwd -P)
    mkdir A
    echo 'int value;' > A/header.h
    touch A/pch.cpp
    $COMPILER -std=c++11 --relocatable-pch -isysroot "$root/A" -Xclang -fno-pch-timestamp -x c++-header -include "$root/A/header.h" -c "$root/A/pch.cpp" -o pch.pch 2>/dev/null || {
        echo "compiler cannot create a relocatable PCH"
        return
    }
    mv A B
    echo 'int f() { return value; }' > consumer.cpp
    if ! $COMPILER -std=c++11 --relocatable-pch -isysroot "$root/B" -include-pch pch.pch -c consumer.cpp -o consumer.o 2>/dev/null; then
        echo "compiler cannot consume a relocated PCH with the original path missing"
    fi
}

SUITE_path_mapping_pch_SETUP() {
    unset CCACHE_NODIRECT
}

SUITE_path_mapping_pch() {
    TEST "Relocate a PCH and consume it with the original checkout unavailable"

    local test_root
    test_root=$(pwd -P)
    mkdir -p A/source A/inputs B/deeper
    cat > A/source/header.h <<'HEADER'
#pragma once
constexpr int answer() { return 42; }
HEADER
    echo '#include "../source/header.h"' > A/inputs/pch.h
    touch A/inputs/pch.cxx
    echo 'static_assert(answer() == 42, "header contents"); int f() { return answer(); }' > A/source/test.cpp
    backdate A/source/header.h A/inputs/pch.h A/inputs/pch.cxx A/source/test.cpp
    cp -R A B/deeper/checkout

    export CCACHE_DEPEND=1 CCACHE_NOHASHDIR=1
    export CCACHE_SLOPPINESS=pch_defines,time_macros
    local checkout build
    local flags=()
    for checkout in "$test_root/A" "$test_root/B/deeper/checkout"; do
        if [ "$checkout" = "$test_root/A" ]; then
            build="$checkout/build-a"
        else
            build="$checkout/generated/different/build-b"
        fi
        mkdir -p "$build"
        export CCACHE_PATH_MAPPING="$build=/__build:$checkout=/__checkout"
        flags=(-std=c++11 --relocatable-pch -isysroot "$checkout"
               -Xclang -fno-pch-timestamp -ffile-prefix-map="$checkout=/__checkout")
        $CCACHE_COMPILE "${flags[@]}" -MD -MF "$build/pch.d" -c -x c++-header -include "$checkout/inputs/pch.h" "$checkout/inputs/pch.cxx" -o "$build/pch.pch"
        $CCACHE_COMPILE "${flags[@]}" -MD -MF "$build/test.d" -Xclang -include-pch -Xclang "$build/pch.pch" -c "$checkout/source/test.cpp" -o "$build/test.o"
        if [ "$checkout" = "$test_root/A" ]; then
            cp "$build/pch.pch" expected.pch
            expect_stat cache_miss 2
            mv A A-unavailable
        fi
    done
    expect_missing A
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 2
    expect_equal_content expected.pch "$build/pch.pch"
    expect_content_pattern "$build/test.d" "*$checkout/source/test.cpp*"
    expect_content_pattern "$build/test.d" "!*$test_root/A/*"

    echo 'static_assert(answer() == 42, "restored PCH"); int changed() { return answer(); }' > "$checkout/source/test.cpp"
    $COMPILER "${flags[@]}" -Xclang -include-pch -Xclang "$build/pch.pch" -c "$checkout/source/test.cpp" -o "$build/direct.o"
    $CCACHE_COMPILE "${flags[@]}" -MD -MF "$build/test.d" -Xclang -include-pch -Xclang "$build/pch.pch" -c "$checkout/source/test.cpp" -o "$build/test.o"
    expect_equal_object_files "$build/direct.o" "$build/test.o"
    expect_stat cache_miss 3
    expect_equal_content expected.pch "$build/pch.pch"

    sed 's/return 42/return 43/' "$checkout/source/header.h" > changed.h
    cp changed.h "$checkout/source/header.h"
    backdate "$checkout/source/header.h"
    $CCACHE_COMPILE "${flags[@]}" -MD -MF "$build/pch.d" -c -x c++-header -include "$checkout/inputs/pch.h" "$checkout/inputs/pch.cxx" -o "$build/pch.pch"
    expect_stat cache_miss 4
    echo 'static_assert(answer() == 43, "changed header"); int changed() { return answer(); }' > "$checkout/source/test.cpp"
    $CCACHE_COMPILE "${flags[@]}" -MD -MF "$build/test.d" -Xclang -include-pch -Xclang "$build/pch.pch" -c "$checkout/source/test.cpp" -o "$build/test.o"
    expect_stat cache_miss 5

    TEST "A same-size change behind symlink/.. invalidates a mapped manifest"

    test_root=$(pwd -P)
    mkdir -p A/source/include A/source/actual/nested
    ln -s ../actual/nested A/source/include/link
    echo '#define VALUE 42' > A/source/include/header.h
    cp A/source/include/header.h A/source/actual/header.h
    echo '#include "include/link/../header.h"' > A/source/test.c
    echo 'int f(void) { return VALUE; }' >> A/source/test.c
    backdate A/source/include/header.h A/source/actual/header.h A/source/test.c
    cp -R A B
    export CCACHE_DEPEND=1 CCACHE_NOHASHDIR=1
    for checkout in "$test_root/A" "$test_root/B"; do
        export CCACHE_PATH_MAPPING="$checkout=/__checkout"
        $CCACHE_COMPILE -MD -MP -MF test.d -c "$checkout/source/test.c" -o test.o
        if [ "$checkout" = "$test_root/A" ]; then
            mv A A-unavailable
        fi
    done
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_content_pattern test.d "*$test_root/B/source/include/link/../header.h*"
    expect_content_pattern test.d "!*$test_root/A/*"
    echo '#define VALUE 43' > B/source/actual/header.h
    backdate B/source/actual/header.h
    $COMPILER -c "$checkout/source/test.c" -o expected.o
    $CCACHE_COMPILE -MD -MP -MF test.d -c "$checkout/source/test.c" -o test.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 1
    expect_equal_object_files expected.o test.o
}
