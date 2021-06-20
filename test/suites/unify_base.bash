# =============================================================================

SUITE_unify_base_SETUP() {
    echo "disabled"
    SUITE_base_SETUP
    export CCACHE_UNIFY=1
    export CCACHE_SLOPPINESS=${CCACHE_SLOPPINESS},unify_with_output 
}

SUITE_unify_base() {
    base_tests
}

# =============================================================================
