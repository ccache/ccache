SUITE_pch_PROBE() {
    touch pch.h
    if ! $REAL_COMPILER $SYSROOT -fpch-preprocess pch.h 2>/dev/null \
            || [ ! -f pch.h.gch ]; then
        echo "compiler ($($COMPILER --version | head -1)) doesn't support precompiled headers"
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
    # - Both GCC and Clang keep an absolute path reference to the original
    # file except that Clang uses that reference to validate the pch and GCC
    # ignores the reference (i.e. the original file can be removed).
    # - Clang can only use pch headers on the command line and not as an #include
    # statement inside a source file.
    # - Clang has -include-pch to directly include a PCH file without any magic
    # of searching for a .gch file.
    #
    # Therefore have common tests and do dedicated tests only for differences.

    pch_suite_common
    if $COMPILER_TYPE_CLANG; then
        pch_suite_clang
    else
        pch_suite_gcc
    fi
}

pch_suite_common() {
    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, without opt-in"

    $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, without opt-in"

    $CCACHE_COMPILE pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Create .gch, -c, no -o, with opt-in"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists pch.h.gch

    # -------------------------------------------------------------------------
    TEST "Create .gch, no -c, -o, with opt-in"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT pch.h -o pch.gch
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists pch.gch

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, #include"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c pch.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Preprocessor error because GCC can't find the real include file when
    # trying to preprocess:
    expect_stat 'preprocessor error' 1
}

pch_suite_gcc() {
    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, no sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, no sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, #include, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch
    rm pch.h

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # GCC seems to cope with this...
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Create and use .gch directory"

    mkdir pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -x c-header -c pch.h -o pch.h.gch/foo
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm pch.h.gch/foo

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -x c-header -c pch.h -o pch.h.gch/foo
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists pch.h.gch/foo

    backdate pch.h.gch/foo

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    echo "updated" >>pch.h.gch/foo # GCC seems to cope with this...
    backdate pch.h.gch/foo

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 3

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, PCH_EXTSUM=1"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "other checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    echo "original checksum" > pch.h.gch.sum
    CCACHE_PCH_EXTSUM=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, no PCH_EXTSUM"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    echo "original checksum" > pch.h.gch.sum

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # external checksum not used, so no cache miss when changed
    echo "other checksum" > pch.h.gch.sum
    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
}

pch_suite_clang() {
    # -------------------------------------------------------------------------
    TEST "Create .gch, include file mtime changed"

    backdate test.h
    cat <<EOF >pch2.h
    #include <stdlib.h>
    #include "test.h"
EOF

    # Make sure time_of_compilation is at least one second larger than the ctime
    # of the test.h include, otherwise we might not cache its ctime/mtime.
    sleep 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    touch test.h
    sleep 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    $REAL_COMPILER $SYSROOT -c -include pch2.h pch2.c
    expect_file_exists pch2.o

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch2.h
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, no sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c 2>/dev/null
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, no -fpch-preprocess, -include, sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, -fpch-preprocess, -include, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # clang seems to cope with this...
    backdate pch.h.gch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .gch, preprocessor mode, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.gch # clang seems to cope with this...
    backdate pch.h.gch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Create .pch, -c, -o"

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.pch
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    rm -f pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS pch_defines" $CCACHE_COMPILE $SYSROOT -c pch.h -o pch.h.pch
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_file_exists pch.h.pch

    # -------------------------------------------------------------------------
    TEST "Use .pch, no -fpch-preprocess, -include, no sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    # Must enable sloppy time macros:
    expect_stat "can't use precompiled header" 1

    # -------------------------------------------------------------------------
    TEST "Use .pch, no -fpch-preprocess, -include, sloppiness"

    $REAL_COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h pch2.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .pch, -fpch-preprocess, -include, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.pch # clang seems to cope with this...
    backdate pch.h.pch

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    # -------------------------------------------------------------------------
    TEST "Use .pch, preprocessor mode"

    $REAL_COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 1

    # -------------------------------------------------------------------------
    TEST "Use .pch, preprocessor mode, file changed"

    $REAL_COMPILER $SYSROOT -c pch.h -o pch.h.pch
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1

    echo "updated" >>pch.h.pch # clang seems to cope with this...
    backdate pch.h.pch

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 2

    CCACHE_NODIRECT=1 CCACHE_SLOPPINESS="$DEFAULT_SLOPPINESS time_macros" $CCACHE_COMPILE $SYSROOT -c -include pch.h -fpch-preprocess pch.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 1
    expect_stat 'cache miss' 2
}
