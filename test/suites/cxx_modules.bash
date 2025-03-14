SUITE_cxx_modules_PROBE() {
    if $COMPILER_TYPE_CLANG; then
        echo "Test not yet supported for Clang"
        return
    fi

    if $COMPILER_TYPE_GCC; then
        # Probe that real compiler actually supports the C++20 module features needed.
        if ! $COMPILER -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=probe.ddi -MD -MF probe.d -o probe.o -x c++ -c - </dev/null >/dev/null 2>&1; then
            echo "compiler $COMPILER (version: $compiler_version) does not support expected C++20 module features"
        fi
        return
    fi

    if $COMPILER_USES_MSVC; then
        echo "Test not yet supported for MSVC"
        return
    fi

    echo "Compiler not known to support C++20 modules"
}

SUITE_cxx_modules_SETUP() {
    echo "setup"

    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1

    cat <<EOF >A.h
#include <iostream>
EOF

    cat <<EOF >B.h
#include <cstdlib>
EOF

    cat <<EOF >foo.cppm
module;
#include "A.h"

export module foo;

export void
f()
{
    std::cout << "hello, world\n";
}
EOF

    cat <<EOF >bar.cppm
module;
#include "B.h"

export module bar;

export void
g()
{
    exit(EXIT_SUCCESS);
}
EOF

    cat <<EOF >main.cpp
import foo;
import bar;

auto
main() -> int
{
    f();
    g();
    return 0;
}
EOF

    backdate A.h
    backdate B.h
    backdate foo.cppm
    backdate bar.cppm
    backdate main.cpp
}

SUITE_cxx_modules() {
    headers=("A.h" "B.h")
    bmi_names=("foo" "bar")

    # -------------------------------------------------------------------------
    TEST "Check that C++ module BMIs are included in manifests"

    if $COMPILER_TYPE_GCC; then
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=foo.ddi -MD -MF foo.d -o foo.o -x c++ -c foo.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=bar.ddi -MD -MF bar.d -o bar.o -x c++ -c bar.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=main.ddi -MD -MF main.d -c main.cpp
    fi

    expect_stat direct_cache_hit 0
    expect_stat cache_miss 3
    expect_stat files_in_cache 6

    manifests=$(find "$CCACHE_DIR" -name '*M' -exec sh -c 'manifest="$1"; "$CCACHE" --inspect "$manifest"' shell {} \;)

    for header in "${headers[@]}"; do
        if ! grep -q "${header}" <<< "$manifests"; then
            test_failed "file pattern \"${header}\" should match against manifest files"
        fi
    done

    if $COMPILER_TYPE_GCC; then
        bmi_dir="gcm\\.cache"
        bmi_ext="gcm"
        for bmi_name in "${bmi_names[@]}"; do
            local bmi="${bmi_name}\\.${bmi_ext}"
            if ! grep -q "${bmi_dir}[/\\]${bmi}" <<< "$manifests"; then
                test_failed "file pattern \"${bmi_dir}/${bmi}\" should match against manifest files"
            fi
        done
    fi

    rm -rf ./*.d ./*.ddi ./*.o

    if $COMPILER_TYPE_GCC; then
        rm -rf gcm.cache
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=foo.ddi -MD -MF foo.d -o foo.o -x c++ -c foo.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=bar.ddi -MD -MF bar.d -o bar.o -x c++ -c bar.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -fdeps-format=p1689r5 -fdeps-file=main.ddi -MD -MF main.d -c main.cpp
    fi

    expect_stat direct_cache_hit 3
    expect_stat cache_miss 3
    expect_stat files_in_cache 6
}
