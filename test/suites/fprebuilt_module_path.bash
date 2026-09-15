# Verify that the module files reached via -fprebuilt-module-path= are part of
# the hash, so that changes to a module (e.g. template function bodies)
# invalidate cached consumer object files.
#
# Clang resolves an import by searching the directory for <module-name>.pcm, so
# unlike -fmodule-file= the command line does not say which file was read.
# Without hashing those files the consumer gets a false cache hit after the
# module changes and silently runs stale code.

SUITE_fprebuilt_module_path_PROBE() {
    if ! $COMPILER_TYPE_CLANG || $COMPILER_USES_MSVC; then
        echo "-fprebuilt-module-path not supported by compiler"
    else
        echo 'export module probe_module;' >probe_module.ixx
        $COMPILER -std=gnu++23 -x c++-module --precompile \
            probe_module.ixx -o probe_module.pcm 2>/dev/null \
            || echo "compiler does not support C++ modules"
    fi
}

SUITE_fprebuilt_module_path_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1

    mkdir -p prebuilt

    cat <<'EOF' >module.ixx
export module somemodule;
export template<typename T>
int module_test() {
    return 1;
}
EOF

    cat <<'EOF' >main.cpp
import somemodule;
int main() {
    return module_test<int>();
}
EOF

    $COMPILER -std=gnu++23 -x c++-module --precompile module.ixx \
        -o prebuilt/somemodule.pcm
}

SUITE_fprebuilt_module_path() {
    # -------------------------------------------------------------------------
    TEST "consumer compile is cached and hits while the module is unchanged"

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    # Searching a directory should not make the compilation uncacheable.
    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 1

    # -------------------------------------------------------------------------
    TEST "template body change invalidates the consumer cache"

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    # Change a template function body in the module and regenerate the pcm.
    cat <<'EOF' >module.ixx
export module somemodule;
export template<typename T>
int module_test() {
    return 2;
}
EOF
    $COMPILER -std=gnu++23 -x c++-module --precompile module.ixx \
        -o prebuilt/somemodule.pcm

    # The consumer must NOT be served from the cache: its object file would
    # contain the old instantiated template body.
    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 0

    # The object file must match what the compiler produces on its own.
    $COMPILER -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o expected.o
    expect_equal_content main.o expected.o

    # -------------------------------------------------------------------------
    TEST "module change invalidates the consumer when no directory is given"

    # Clang resolves an empty value against the working directory.
    cp prebuilt/somemodule.pcm somemodule.pcm

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path= -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    cat <<'EOF' >module.ixx
export module somemodule;
export template<typename T>
int module_test() {
    return 2;
}
EOF
    $COMPILER -std=gnu++23 -x c++-module --precompile module.ixx \
        -o somemodule.pcm

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path= -c main.cpp -o main.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 0

    $COMPILER -std=gnu++23 -fprebuilt-module-path= -c main.cpp -o expected.o
    expect_equal_content main.o expected.o

    # -------------------------------------------------------------------------
    TEST "module file added to the searched directory invalidates the consumer"

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    # An import resolves to a file in the directory, so all module files in it
    # are hashed even though the compilation only reads one of them.
    cat <<'EOF' >other.ixx
export module othermodule;
export int other_test() { return 1; }
EOF
    $COMPILER -std=gnu++23 -x c++-module --precompile other.ixx \
        -o prebuilt/othermodule.pcm

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 0

    # -------------------------------------------------------------------------
    TEST "renaming a module file in the searched directory invalidates the consumer"

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    # An import resolves to <module-name>.pcm, so the directory no longer
    # provides somemodule and the compilation must fail. The contents of the
    # files in the directory are unchanged.
    mv prebuilt/somemodule.pcm prebuilt/renamed.pcm

    if $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt \
            -c main.cpp -o main.o >&/dev/null; then
        test_failed "consumer compiled without the module it imports"
    fi
    expect_stat direct_cache_hit 0

    # -------------------------------------------------------------------------
    TEST "a directory named like a module file does not prevent caching"

    # Only the module files are read, so a directory that happens to share
    # their extension is not an input.
    mkdir prebuilt/subdir.pcm

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat bad_input_file 0

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat direct_cache_hit 1
    expect_stat bad_input_file 0

    if ! $HOST_OS_WINDOWS && ! $HOST_OS_CYGWIN; then
        # ---------------------------------------------------------------------
        TEST "an unreadable module file does not prevent caching"

        # The compiler cannot read a dangling symlink either, so it is not an
        # input to a compilation that does not import it.
        ln -s missing prebuilt/othermodule.pcm

        $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
        expect_stat cache_miss 1
        expect_stat could_not_use_modules 0

        $CCACHE_COMPILE -std=gnu++23 -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
        expect_stat cache_miss 1
        expect_stat direct_cache_hit 1
    fi

    # -------------------------------------------------------------------------
    TEST "an implicit prebuilt module search is uncacheable"

    $CCACHE_COMPILE -std=gnu++23 -fprebuilt-implicit-modules \
        -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat could_not_use_modules 1

    # -------------------------------------------------------------------------
    TEST "modules sloppiness makes an implicit prebuilt module search cacheable"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -std=gnu++23 \
        -fprebuilt-implicit-modules -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat could_not_use_modules 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -std=gnu++23 \
        -fprebuilt-implicit-modules -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 1

    # The sloppiness does not stop the searched module files from being hashed.
    cat <<'EOF' >module.ixx
export module somemodule;
export template<typename T>
int module_test() {
    return 2;
}
EOF
    $COMPILER -std=gnu++23 -x c++-module --precompile module.ixx \
        -o prebuilt/somemodule.pcm

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -std=gnu++23 \
        -fprebuilt-implicit-modules -fprebuilt-module-path=prebuilt -c main.cpp -o main.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 1
}
