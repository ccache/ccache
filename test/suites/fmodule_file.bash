# Verify that the contents of explicitly imported module files (-fmodule-file=)
# are part of the hash, so that changes to a module (e.g. template function
# bodies) invalidate cached consumer object files.
#
# A precompiled module file (pcm) is not part of the preprocessed
# output of a consumer translation unit, so without explicit hashing the consumer
# gets a false cache hit after the module changes and silently runs stale code.

SUITE_fmodule_file_PROBE() {
    if ! $COMPILER_TYPE_CLANG || $COMPILER_USES_MSVC; then
        echo "-fmodule-file not supported by compiler"
    else
        echo 'export module probe_module;' >probe_module.ixx
        $COMPILER -std=gnu++23 -x c++-module -fmodule-output=probe_module.pcm \
            -c probe_module.ixx -o probe_module.o 2>/dev/null \
            || echo "compiler does not support C++ modules"
    fi
}

SUITE_fmodule_file_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1

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
}

SUITE_fmodule_file() {
    # -------------------------------------------------------------------------
    TEST "consumer compile is cached and hits while the module is unchanged"

    $COMPILER -std=gnu++23 -x c++-module -fmodule-output=somemodule.pcm -c module.ixx -o module.o
    $CCACHE_COMPILE -std=gnu++23 -fmodule-file=somemodule=somemodule.pcm -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 0

    # Same directory, same module: the consumer must be a direct cache hit.
    $CCACHE_COMPILE -std=gnu++23 -fmodule-file=somemodule=somemodule.pcm -c main.cpp -o main.o
    expect_stat cache_miss 1
    expect_stat direct_cache_hit 1

    # -------------------------------------------------------------------------
    TEST "template body change invalidates the consumer cache"

    $COMPILER -std=gnu++23 -x c++-module -fmodule-output=somemodule.pcm -c module.ixx -o module.o
    $CCACHE_COMPILE -std=gnu++23 -fmodule-file=somemodule=somemodule.pcm -c main.cpp -o main.o
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
    $COMPILER -std=gnu++23 -x c++-module -fmodule-output=somemodule.pcm -c module.ixx -o module.o

    # The consumer must NOT be served from the cache: its object file would contain the old instantiated template body.
    $CCACHE_COMPILE -std=gnu++23 -fmodule-file=somemodule=somemodule.pcm -c main.cpp -o main.o
    expect_stat cache_miss 2
    expect_stat direct_cache_hit 0
}
