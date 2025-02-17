cxx_modules_features_unsupported() {
    # shellcheck disable=SC2154
    echo "compiler $COMPILER (version: $compiler_version) does not support expected C++20 module features"
}

cxx_modules_flag_gcc() {
    CXX_MODULES_PROBE_GCC=(-std=c++20 -fdeps-format=p1689r5 -fdeps-file=probe.ddi -MMD -MF probe.d -o probe.o -x c++ -c -)
    # Check if `g++ -fmodules` or `g++ -fmodules-ts` works (and remember which).
    for flag in -fmodules -fmodules-ts; do
        if $COMPILER "$flag" "${CXX_MODULES_PROBE_GCC[@]}" </dev/null >/dev/null 2>&1; then
            echo $flag
            return
        fi
    done
}

SUITE_cxx_modules_PROBE() {
    if ! $CCACHE --version | grep -Fq -- C++20-modules &> /dev/null; then
        echo "C++20-modules not available"
        return
    fi

    # Probe that real compilers actually support C++20 module features needed.

    if $COMPILER_TYPE_CLANG; then
        echo "Test not yet supported for Clang"
        return
    fi

    if $COMPILER_TYPE_GCC; then
        if [[ -z "$(cxx_modules_flag_gcc)" ]]; then
            cxx_modules_features_unsupported
        fi
        return
    fi

    if $COMPILER_USES_MSVC; then
        echo "Test not yet supported for MSVC"
        return
    fi

    echo "compiler $COMPILER (version: $compiler_version) not known to support expected C++20 module features"
}

SUITE_cxx_modules_SETUP() {
    echo "setup"

    unset CCACHE_NODIRECT
    unset CCACHE_NODEPEND
    unset CCACHE_NOCXX_MODULES

    export CCACHE_DIRECT=1
    export CCACHE_DEPEND=1
    export CCACHE_CXX_MODULES=1

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

    if $COMPILER_TYPE_GCC; then
        CXX_MODULES_FLAG_GCC=$(cxx_modules_flag_gcc)

        # -------------------------------------------------------------------------
        TEST "GCC: require ccache direct mode"

        # shellcheck disable=SC2086
        CCACHE_NODIRECT=1 env -u CCACHE_DIRECT $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm

        expect_stat files_in_cache 0

        # -------------------------------------------------------------------------
        TEST "GCC: require ccache depend mode"

        # shellcheck disable=SC2086
        CCACHE_NODEPEND=1 env -u CCACHE_DEPEND $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm

        expect_stat files_in_cache 0

        # -------------------------------------------------------------------------
        TEST "GCC: require ccache C++20 modules mode"

        # shellcheck disable=SC2086
        CCACHE_NOCXX_MODULES=1 env -u CCACHE_CXX_MODULES $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm

        expect_stat files_in_cache 0

        # -------------------------------------------------------------------------
        TEST "GCC: BMIs are included in manifests (enhanced depfiles)"

        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF bar.d -o bar.o -x c++ -c bar.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF main.d -c main.cpp

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

        rm -rf gcm.cache
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF bar.d -o bar.o -x c++ -c bar.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -MMD -MF main.d -c main.cpp

        expect_stat direct_cache_hit 3
        expect_stat cache_miss 3
        expect_stat files_in_cache 6

        # -------------------------------------------------------------------------
        TEST "GCC: BMIs are included in manifests (p1689r5 DDIs)"

        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=foo.ddi -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=bar.ddi -MMD -MF bar.d -o bar.o -x c++ -c bar.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=main.ddi -MMD -MF main.d -c main.cpp

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

        rm -rf gcm.cache
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=foo.ddi -MMD -MF foo.d -o foo.o -x c++ -c foo.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=bar.ddi -MMD -MF bar.d -o bar.o -x c++ -c bar.cppm
        $CCACHE_COMPILE -std=c++20 "$CXX_MODULES_FLAG_GCC" -fdeps-format=p1689r5 -fdeps-file=main.ddi -MMD -MF main.d -c main.cpp

        expect_stat direct_cache_hit 3
        expect_stat cache_miss 3
        expect_stat files_in_cache 6
    fi
}
