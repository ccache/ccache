SUITE_modules_PROBE() {
    if ! $COMPILER_TYPE_CLANG || $COMPILER_USES_MSVC; then
        echo "-fmodules/-fcxx-modules not supported by compiler"
    else
        cat <<EOF >testmodules.cpp
#include <string>
EOF
        $COMPILER -x c++ -fmodules testmodules.cpp -S || echo "compiler does not support modules"
    fi
}

SUITE_modules_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_DEPEND=1

    cat <<EOF >test1.h
#include <string>
EOF
    backdate test1.h

cat <<EOF >module.modulemap
module "Test1" {
  header "test1.h"
  export *
}
EOF
    backdate module.modulemap

   cat <<EOF >test1.cpp
#import "test1.h"
int main() { return 0; }
EOF
}

SUITE_modules() {
    # -------------------------------------------------------------------------
    TEST "fall back to real compiler, no sloppiness"

    $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat could_not_use_modules 1

    $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat could_not_use_modules 2

    # -------------------------------------------------------------------------
    TEST "fall back to real compiler, no depend mode"

    unset CCACHE_DEPEND

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat could_not_use_modules 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat could_not_use_modules 2

    # -------------------------------------------------------------------------
    TEST "cache hit"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "cache miss"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -MD -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat cache_miss 1

    cat <<EOF >test1.h
#include <string>
void f();
EOF
    backdate test1.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -MD -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat cache_miss 2

    echo >>module.modulemap
    backdate test1.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS modules" $CCACHE_COMPILE -MD -x c++ -fmodules -fcxx-modules -c test1.cpp -MD
    expect_stat cache_miss 3
}
