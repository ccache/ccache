SUITE_pch_PROBE() {
    touch pch.h empty.c
    mkdir dir

    if ! $COMPILER $SYSROOT -fpch-preprocess pch.h 2>/dev/null \
            || [ ! -f pch.h.gch ]; then
        echo "compiler ($($COMPILER --version | head -n 1)) doesn't support precompiled headers"
    fi

    $COMPILER $SYSROOT -c pch.h -o dir/pch.h.gch 2>/dev/null
    if ! $COMPILER $SYSROOT -c -include dir/pch.h empty.c 2>/dev/null; then
        echo "compiler ($($COMPILER --version | head -n 1)) seems to have broken support for precompiled headers"
    fi
}

SUITE_pch_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >pch.c
#include "pch.h"
int main()
{
  void *p = NULL;
  return 0;
}
EOF
    cat <<EOF >pch.h
#include <stdlib.h>
EOF
    backdate pch.h
    cat <<EOF >pch2.c
int main()
{
  void *p = NULL;
  return 0;
}
EOF
}

SUITE_pch() {
    # Clang should generally be compatible with GCC and so most of the tests
    # can be shared. There are some differences though:
    #
    # - Both GCC and Clang keep an absolute path reference to the original file
    #   except that Clang uses that reference to validate the pch and GCC
    #   ignores the reference (i.e. the original file can be removed).
    # - Clang can only use pch headers on the command line and not as an
    #   #include statement inside a source file, because it silently ignores
    #   -fpch-preprocess and does not output pragma GCC pch_preprocess.
    # - Clang has -include-pch to directly include a PCH file without any magic
    #   of searching for a .gch file.
    #
    # Put tests that work with both compilers in pch_suite_common and put
    # compiler-specific tests in pch_suite_clang/pch_suite_gcc.

    pch_suite_common
    if $COMPILER_TYPE_CLANG; then
        if ! [ $COMPILER_BIN = "gcc" ] || $RUN_WIN_XFAIL; then
            pch_suite_clang
        fi
    else
        pch_suite_gcc
    fi
}

pch_suite_common() {
    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, without opt-in"

    $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat could_not_use_precompiled_header 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, without opt-in"

    $CCACHE_COMPILE pch.h -o pch.gch
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat could_not_use_precompiled_header 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, with opt-in"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    rm pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists pch.h.gch

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    rm pch.h.gch
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_exists pch.h.gch

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, with opt-in"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists pch.gch

    # -------------------------------------------------------------------------
    TEST "Use .gch, #include, remove pch.h"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c pch.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    # Preprocessor error because GCC can't find the real include file when
    # trying to preprocess (gcc -E will be called by ccache):
    expect_stat preprocessor_error 1

    # -------------------------------------------------------------------------
    include_pch_variants=(
        "-include pch.h"
        "-includepch.h"
    )
    for args in "${include_pch_variants[@]}"; do
        TEST "Use .gch, $args, no sloppiness"

        $COMPILER $SYSROOT -c pch.h
        backdate pch.h.gch

        $CCACHE_COMPILE $SYSROOT -c $args pch2.c
        expect_stat direct_cache_hit 0
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 0
        # Must enable sloppy time macros:
        expect_stat could_not_use_precompiled_header 1
    done

    # -------------------------------------------------------------------------
    TEST "Use .gch, -include"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

if $RUN_WIN_XFAIL; then
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
fi

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, -include"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, -o"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.gch
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    rm -f pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.gch
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists pch.h.gch

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    rm pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.gch
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_exists pch.h.gch

    # -------------------------------------------------------------------------
    TEST "Use .gch, -include, PCH_EXTSUM=1"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo "other checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    echo "original checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # With GCC, a newly generated PCH is always different, even if the contents
    # should be exactly the same. And Clang stores file timestamps, so in this
    # case the PCH is different too. So without .sum a "changed" PCH would mean
    # a miss, but if the .sum doesn't change, it should be a hit.

    sleep 1
    touch pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, -include, no PCH_EXTSUM"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # external checksum not used, so no cache miss when changed
    echo "other checksum" > pch.h.gch.sum
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -include, other dir for .gch"

    mkdir -p dir
    $COMPILER $SYSROOT -c pch.h -o dir/pch.h.gch
    backdate dir/pch.h.gch
    rm -f pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o dir/pch.h.gch
    backdate dir/pch.h.gch

if $RUN_WIN_XFAIL; then
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
fi
    rm -rf dir

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, -include, other dir for .gch"

    mkdir -p dir
    $COMPILER $SYSROOT -c pch.h -o dir/pch.h.gch
    backdate dir/pch.h.gch
    rm -f pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o dir/pch.h.gch
    backdate dir/pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include dir/pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
    rm -rf dir

    # -------------------------------------------------------------------------
    TEST "Use .gch, depend mode, -include"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_DEPEND=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c -MD -MF pch.d
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_DEPEND=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c -MD -MF pch.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_DEPEND=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c -MD -MF pch.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_DEPEND=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c -MD -MF pch.d
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
}

pch_suite_gcc() {
    # -------------------------------------------------------------------------
    TEST "Use .gch, -include, remove pch.h"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, #include, no sloppiness"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    # Must enable sloppy time macros:
    expect_stat could_not_use_precompiled_header 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, #include"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

if $RUN_WIN_XFAIL; then
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
fi

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, #include"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

if $RUN_WIN_XFAIL; then
    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2
fi

    # -------------------------------------------------------------------------
    TEST "Create and use .gch directory"

    mkdir pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -x c-header -c pch.h -o pch.h.gch/foo
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    rm pch.h.gch/foo

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -x c-header -c pch.h -o pch.h.gch/foo
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists pch.h.gch/foo

    backdate pch.h.gch/foo

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    echo "updated" >>pch.h.gch/foo # GCC seems to cope with this...
    backdate pch.h.gch/foo

if $RUN_WIN_XFAIL; then
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 3
fi

    # -------------------------------------------------------------------------
    TEST "Use .gch, #include, PCH_EXTSUM=1"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo "other checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    echo "original checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # With GCC, a newly generated PCH is always different, even if the contents
    # should be exactly the same. And Clang stores file timestamps, so in this
    # case the PCH is different too. So without .sum a "changed" PCH would mean
    # a miss, but if the .sum doesn't change, it should be a hit.

    sleep 1
    touch pch.h
    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 3
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, #include, no PCH_EXTSUM"

    $COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # external checksum not used, so no cache miss when changed
    echo "other checksum" > pch.h.gch.sum
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # -------------------------------------------------------------------------
    TEST "Too new PCH file"

    # If the precompiled header is too new we shouldn't cache the result at all
    # since:
    #
    # - the precompiled header content must be included in the hash, but
    # - we don't trust the precompiled header content so we can't hash it
    #   ourselves, and
    # - the preprocessed output doesn't contain the preprocessed header content.

    touch lib.h
    touch main.c

    $COMPILER $SYSROOT -c lib.h
    touch -d "@$(($(date +%s) + 60))" lib.h.gch # 1 minute in the future

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines,time_macros" $CCACHE_COMPILE -include lib.h -c main.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    expect_stat could_not_use_precompiled_header 1
}

pch_suite_clang() {
    # -------------------------------------------------------------------------
    TEST "Create .pch, include file mtime changed"

    backdate test.h
    cat <<EOF >pch2.h
    #include <stdlib.h>
    #include "test.h"
EOF

    # Make sure time_of_compilation is at least one second larger than the ctime
    # of the test.h include, otherwise we might not cache its ctime/mtime.
    sleep 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    touch test.h
    sleep 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    $COMPILER $SYSROOT -c -include pch2.h pch2.c
    expect_exists pch2.o

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .pch, -include, no sloppiness"

    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 0
    # Must enable sloppy time macros:
    expect_stat could_not_use_precompiled_header 1

    # -------------------------------------------------------------------------
    TEST "Use .pch, -include"

    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .pch, preprocessor mode, -include"

    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .pch, -include-pch"

    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch2.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch2.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Use .pch, preprocessor mode, -include-pch"

    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include-pch pch.h.pch pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "Create .pch with -Xclang options"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -Xclang -emit-pch -o pch.h.pch -c pch.h
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    rm pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -Xclang -emit-pch -o pch.h.pch -c pch.h
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_exists pch.h.pch

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    rm pch.h.pch
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -Xclang -emit-pch -o pch.h.pch -c pch.h
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_exists pch.h.pch

    # -------------------------------------------------------------------------
    TEST "Use .pch the with -Xclang options"

    $COMPILER $SYSROOT -Xclang -emit-pch -o pch.h.pch -c pch.h
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -Xclang -include-pch -Xclang pch.h.pch -Xclang -include -Xclang pch.h -c pch.c
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -Xclang -include-pch -Xclang pch.h.pch -Xclang -include -Xclang pch.h -c pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    echo '#include <string.h> /*change pch*/' >>pch.h
    backdate pch.h
    $COMPILER $SYSROOT -Xclang -emit-pch -o pch.h.pch -c pch.h
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -Xclang -include-pch -Xclang pch.h.pch -Xclang -include -Xclang pch.h -c pch.c
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -Xclang -include-pch -Xclang pch.h.pch -Xclang -include -Xclang pch.h -c pch.c
    expect_stat direct_cache_hit 2
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
}
