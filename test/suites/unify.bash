# =============================================================================

SUITE_unify_SETUP() {
    export CCACHE_UNIFY=1
}

SUITE_unify() {
    ORIG_SLOPPINESS="${CCACHE_SLOPPINESS}"

    unify_tests_common "CCACHE_CPP2=1" "${ORIG_SLOPPINESS}"
    unify_tests_common "CCACHE_NOCPP2=1" "${ORIG_SLOPPINESS}"
}

# =============================================================================

unify_tests_common() {
    local TESTENV="$1"
    local ORIG_SLOPPINESS="$2"

    unify_test_cases "$TESTENV CCACHE_SLOPPINESS=${ORIG_SLOPPINESS}"
    unify_test_cases "$TESTENV CCACHE_SLOPPINESS=incbin,${ORIG_SLOPPINESS}"
    unify_test_cases "$TESTENV CCACHE_SLOPPINESS=system_headers,${ORIG_SLOPPINESS}"
    unify_test_cases "$TESTENV CCACHE_SLOPPINESS=unify_with_debug,${ORIG_SLOPPINESS}"
    unify_test_cases "$TESTENV CCACHE_SLOPPINESS=unify_with_diagnostics,${ORIG_SLOPPINESS}"
}


unify_test_cases() {
    local TESTENV="$1"

    # -------------------------------------------------------------------------
    TEST "Unify base case $TESTENV"
    export $TESTENV

    unset CCACHE_NODIRECT
    cat <<EOF >unify.c
// unify.c
#include "unify.h"
EOF
    cat <<EOF >unify.h
// unify.h
int a1;
// some lines that the preprocessor removes
// when not emitting line directives (-P flag)
//
// Note:
// clang (without -fnormalize-whitespace)
// only collapses lines
// when there are more 8 (or more)
// consecutive empty lines.
int b1;
EOF
    backdate unify.h

    # Reference output
    $REAL_COMPILER -c unify.c -o unify-ref.o

    # Seed the cache
    CCACHE_DEBUG=1 $CCACHE_COMPILE -c unify.c -o unify1.o
    expect_equal_object_files unify-ref.o unify1.o
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # result + manifest

    # Expect direct cache hit
    $CCACHE_COMPILE -c unify.c -o unify2.o
    expect_equal_object_files unify-ref.o unify2.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Expect preprocessed hit where -P collapses empty lines
    cat <<EOF >unify.h
// unify.h
int a1;
int b1;
EOF
    backdate unify.h
    CCACHE_DEBUG=1 $CCACHE_COMPILE -c unify.c -o unify3.o
    expect_equal_object_files unify-ref.o unify3.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # Expect miss with real change
    cat <<EOF >unify.h
int a1;
EOF
    backdate unify.h
    $CCACHE_COMPILE -c unify.c -o unify4.o
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2


    # -------------------------------------------------------------------------
    TEST "Incbin $TESTENV"
    export $TESTENV
    cat <<EOF >incbin.c
__asm__(".incbin \"incbin.c\"");
EOF

    # Reference output
    $REAL_COMPILER -c incbin.c -o incbin-ref.o

    # Check file with assembler .incbin
    $CCACHE_COMPILE -c incbin.c -o incbin1.o
    expect_equal_object_files incbin-ref.o incbin1.o
    if [[ "${CCACHE_SLOPPINESS}" = *"incbin"* ]]; then
        # Allow if ignored by sloppiness option
        expect_stat unsupported_code_directive 0
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 1

        $CCACHE_COMPILE -c incbin.c -o incbin2.o
        expect_equal_object_files incbin-ref.o incbin2.o
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1
    else
        # .incbin unaccounted in dependencies 
        expect_stat unsupported_code_directive 1
    fi


    # -------------------------------------------------------------------------
    TEST "Debug info $TESTENV"
    export $TESTENV
    cat <<EOF >normalize.c
// normalize.c
int main() { return 0; }
//
//
//
//
//
//
//
//
//
int i;
EOF

    # Reference output
    $REAL_COMPILER -g -c normalize.c -o normalize-ref.o

    # Cache seeding 
    $CCACHE_COMPILE -g -c normalize.c -o normalize1.o
    expect_equal_object_files normalize-ref.o normalize1.o
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # Make whitespace-line change
    cat <<EOF >normalize.c
// normalize.c
int main() { return 0; }
int i;
EOF
    $CCACHE_COMPILE -g -c normalize.c -o normalize2.o
    if [[ "${CCACHE_SLOPPINESS}" = *"unify_with_debug"* ]]; then
        # Expect hit with sloppy; debug info may point to the wrong lines
        expect_equal_object_files normalize-ref.o normalize2.o
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1
    else
        # Unify mode is disabled implicitly in debug mode
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 2
    fi


    # -------------------------------------------------------------------------
    TEST "Diagnostic output $TESTENV"
    export $TESTENV
    unset CCACHE_NODIRECT

    # Every compiler should emit at least one warning, even without additional flags such was -Wall.
    cat <<EOF >diag.c
// diag.c
double main() {
// Compiler warning
int x = x / 0; // diag.c:6:11: warning: division by zero [-Wdiv-by-zero]
} 
EOF

    # Reference output
    $REAL_COMPILER -c diag.c -o diag-ref.o 2>/dev/null

    # Seed cache
    $CCACHE_COMPILE -c diag.c -o diag1.o 2>/dev/null
    expect_equal_object_files diag-ref.o diag1.o
    expect_stat preprocessed_cache_hit 0
    if [[ -n "$CCACHE_NOCPP2" && "${CCACHE_SLOPPINESS}" != *"unify_with_diagnostics"* ]]; then
        # Any output in unify+nocpp2 mode results in a fallback; diagnostics would refer to temporary file
        expect_stat compiler_produced_stderr 1
        expect_stat cache_miss 0
    else
        expect_stat compiler_produced_stderr 0
        expect_stat cache_miss 1

        # Make whitespace-only change
        cat <<EOF >diag.c
//
double main() {
//
int x = x / 0;
}
EOF
        $CCACHE_COMPILE -c diag.c -o diag2.o 2>/dev/null
        expect_equal_object_files diag-ref.o diag2.o
        if [[ "${CCACHE_SLOPPINESS}" = *unify_with_diagnostics* ]]; then
            # Expect hit with sloppy; diagnostics may point to the wrong lines
            expect_stat preprocessed_cache_hit 1
            expect_stat cache_miss 1
        else
            # Don't match with output
            expect_stat preprocessed_cache_hit 0
            expect_stat cache_miss 2

            # Direct mode can still match last run
            $CCACHE_COMPILE -c diag.c -o diag3.o 2>/dev/null
            expect_equal_object_files diag-ref.o diag3.o
            expect_stat direct_cache_hit 1
            expect_stat preprocessed_cache_hit 0
            expect_stat cache_miss 2
        fi
    fi

    # -------------------------------------------------------------------------
    unify_tests_depfile "$TESTENV" "-MD"
    unify_tests_depfile "$TESTENV" "-MMD"
}

unify_tests_depfile() {
    TESTENV="$1"
    DEPLVL="$2"

    # -------------------------------------------------------------------------
    TEST "$DEPLVL dependency generation $TESTENV"
    export $TESTENV
    unset CCACHE_NODIRECT
    cat <<EOF >sysheader.c
// sysheader.c
#include "sysheader.h"
#include <stdint.h>
int a;
int b;
EOF
    cat <<EOF >sysheader.h
// sysheader.h
#include <stddef.h>
int i;
EOF
    backdate sysheader.h
    cp sysheader.h sysheader-repl.h

    # Reference output
    $REAL_COMPILER -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader-ref.d
    cp sysheader.o sysheader-ref.o

    # Populate cache
    $CCACHE_COMPILE -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader1.d
    expect_equal_object_files sysheader-ref.o sysheader.o
    expect_equal_text_content sysheader-ref.d sysheader1.d
    expect_stat direct_cache_hit 0
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # Expect direct hit without change
    $CCACHE_COMPILE -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader2.d
    expect_equal_object_files sysheader-ref.o sysheader.o
    expect_equal_text_content sysheader-ref.d sysheader2.d
    expect_stat direct_cache_hit 1
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1

    # Expect preprocessed hit after irrelevant change
    cat <<EOF >sysheader.c
// sysheader.c
#include "sysheader.h"
#include <stdint.h>
int a;
// additional lines
//
//
//
//
//
//
//
int b;
EOF
    $CCACHE_COMPILE -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader3.d
    expect_equal_object_files sysheader-ref.o sysheader.o
    expect_equal_text_content sysheader-ref.d sysheader3.d
    expect_stat direct_cache_hit 1
    if [[ "$DEPLVL" == "-MMD" &&  "${CCACHE_SLOPPINESS}" != *"system_headers"* ]]; then
        expect_stat preprocessed_cache_hit 0
        expect_stat cache_miss 2
    else
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 1

        # Expect miss when includes change
        cat <<EOF >sysheader.c
// sysheader.c
#include "sysheader-repl.h"
#include <stdint.h>
int a;
int b;
EOF
        $REAL_COMPILER -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader-repl-ref.d
        $CCACHE_COMPILE -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader4.d
        expect_equal_object_files sysheader-ref.o sysheader.o
        expect_equal_text_content sysheader-repl-ref.d sysheader4.d
        expect_stat direct_cache_hit 1
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 2

        # Same when including a different system header
        cat <<EOF >sysheader.c
// sysheader.c
#include "sysheader.h"
#include <assert.h>
int a;
int b;
EOF
        $REAL_COMPILER -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader-ref.d
        $CCACHE_COMPILE -c sysheader.c -o sysheader.o $DEPLVL -MF sysheader5.d
        expect_equal_object_files sysheader-ref.o sysheader.o
        expect_equal_text_content sysheader-ref.d sysheader5.d
        expect_stat direct_cache_hit 1
        expect_stat preprocessed_cache_hit 1
        expect_stat cache_miss 3
    fi
}
