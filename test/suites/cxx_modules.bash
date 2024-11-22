SUITE_cxx_modules_PROBE() {
    if $COMPILER_TYPE_CLANG; then
        echo FIXME
        return
    fi
    if $COMPILER_TYPE_GCC; then
        return
    fi
    if $COMPILER_USES_MSVC; then
        echo FIXME
        return
    fi
    echo "compiler not known to support C++20 modules"
}

SUITE_cxx_modules_SETUP() {
    echo "setup"

    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1

    cat <<EOF >foo.cppm
export module foo;

export void
f()
{
}
EOF

    cat <<EOF >bar.cppm
export module bar;

export void
g()
{
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

    backdate foo.cppm
    backdate bar.cppm
    backdate main.cpp
}

SUITE_cxx_modules() {
    # -------------------------------------------------------------------------
    TEST "fall back to real compiler"

    if $COMPILER_TYPE_CLANG; then
        exit 1
    fi
    if $COMPILER_TYPE_GCC; then
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -MD -MF foo.d -o foo.o -x c++ -c foo.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -MD -MF bar.d -o bar.o -x c++ -c bar.cppm
        CCACHE_DEPEND=1 $CCACHE_COMPILE -std=c++20 -fmodules-ts -c main.cpp
    fi
    if $COMPILER_USES_MSVC; then
        exit 1
    fi

    expect_stat direct_cache_hit 0
    expect_stat cache_miss 3
    expect_stat files_in_cache 0
}
