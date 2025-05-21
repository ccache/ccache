clang_modules_SETUP() {
    cat <<EOF >A.h
#include <stdio.h>
EOF
    cat <<EOF >module.modulemap
module M {
  header "A.h"
  export *
}
EOF
    cat <<EOF >test.c
#include "A.h" // module M
int main() { return 0; }
EOF
}

SUITE_clang_modules_PROBE() {
    clang_modules_SETUP
    if $COMPILER -fmodules test.c -MD && grep --quiet "module\.modulemap" test.d; then
        return
    fi
    echo "compiler does not support Clang modules"
}

SUITE_clang_modules_SETUP() {
    clang_modules_SETUP
    backdate A.h
    backdate module.modulemap
    $COMPILER -fmodules test.c
    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1
}

SUITE_clang_modules() {
    # -------------------------------------------------------------------------
    TEST "fall back to real compiler, no sloppiness"

    $CCACHE_COMPILE -fmodules test.c -MD
    expect_stat could_not_use_modules 1

    $CCACHE_COMPILE -fmodules test.c -MD
    expect_stat could_not_use_modules 2

    # -------------------------------------------------------------------------
    TEST "fall back to real compiler, no depend mode"

    unset CCACHE_DEPEND

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat could_not_use_modules 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat could_not_use_modules 2

    # -------------------------------------------------------------------------
    TEST "cache hit"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "cache miss"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat cache_miss 1

    cat <<EOF >A.h
#include <stdio.h>
void f();
EOF
    backdate A.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat cache_miss 2

    echo >>module.modulemap
    backdate A.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS clang_modules" $CCACHE_COMPILE -fmodules -c test.c -MD
    expect_stat cache_miss 3
}
