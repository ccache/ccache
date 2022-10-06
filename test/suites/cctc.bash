cctc_PROBE() {
    if [ -z "$REAL_CCTC" ]; then
        echo "ctc is not available"
    fi
}

cctc_SETUP() {
    TASKING_COMPILER=$REAL_CCTC
    TASKING_COMPARE_OUTPUT=expect_cctc_src_equal
    TASKING_TARGET="--create=object"
    TASKING_FLAGS="--core=tc1.6.2 $TASKING_TARGET"
    TASKING_DEPFLAGS="--dep-file $TASKING_FLAGS"
    TASKING_EXTENSION="o"

    CCACHE_TASKING="$CCACHE $TASKING_COMPILER"
}

expect_cctc_src_equal() {
    if [ ! -e "$1" ]; then
        test_failed_internal "expect_cctc_src_equal: $1 missing"
    fi
    if [ ! -e "$2" ]; then
        test_failed_internal "expect_cctc_src_equal: $2 missing"
    fi
# for cctc I do not know how to compare the object files, I would need to
# remove a section
#    # remove the compiler invocation lines that could differ
#    cp $1 $1_for_check
#    cp $2 $2_for_check
#    sed_in_place '/.compiler_invocation/d' $1_for_check $2_for_check
#    sed_in_place '/;source/d' $1_for_check $2_for_check
#
#    if ! cmp -s "$1_for_check" "$2_for_check"; then
#        test_failed_internal "$1 and $2 differ: $(echo; diff -u "$1_for_check" "$2_for_check")"
#    fi
}


SUITE_cctc_PROBE() {
    ${CURRENT_SUITE}_PROBE
}

SUITE_cctc_SETUP() {
    ${CURRENT_SUITE}_SETUP
}

SUITE_cctc() {
    ctc_base_tests
    ctc_basedir_tests
    ctc_depfile_tests
    ctc_dependmode_set1_tests
    ctc_dependmode_set2_tests
}
