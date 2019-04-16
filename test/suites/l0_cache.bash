SUITE_l0_cache_SETUP() {
    unset CCACHE_NODIRECT

    cat <<EOF >test.c
// test.c
#include "test1.h"
EOF
    cat <<EOF >test1.h
int test1;
EOF

    backdate test1.h

    DEPSFLAGS_REAL="-MP -MMD -MF reference_test.d"
    DEPSFLAGS_CCACHE="-MP -MMD -MF test.d"
}

SUITE_l0_cache() {
    # -------------------------------------------------------------------------
    TEST "L0 cache in depend mode"
    CCACHE_DIR_L0="${CCACHE_DIR}/../.ccachel0"
    mkdir -p ${CCACHE_DIR_L0}
    
    # First compile using cache L0 as a CCACHE_DIR to simulate it being copied from elsewhere
    BACKUP_CCACHE_DIR=${CCACHE_DIR}
    export CCACHE_DIR=${CCACHE_DIR_L0}
    $REAL_COMPILER $DEPSFLAGS_REAL -c -o reference_test.o test.c

    CCACHE_DEPEND=1 $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3 # .o + .manifest + .d

    CCACHE_DEPEND=1 $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3
    
    # Switch to using original CCACHE_DIR, it should be empty and contain no objects/manifests,
    # yet gets cache hit due to cache_dir_l0 containing cached results
    # Stats are updated in the L1 dir only
    export CCACHE_DIR=${BACKUP_CCACHE_DIR}
    $CCACHE --clear && $CCACHE --zero-stats
    
    CCACHE_DEPEND=1 CCACHE_DIR_L0="${CCACHE_DIR_L0}" $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 0
    expect_file_count "0" "*.manifest" $CCACHE_DIR
    expect_file_count "0" "*.o" $CCACHE_DIR
    expect_file_count "1" "*.manifest" $CCACHE_DIR_L0
    expect_file_count "1" "*.o" $CCACHE_DIR_L0
    
    CCACHE_DEPEND=1 CCACHE_DIR_L0="${CCACHE_DIR_L0}" $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 0
    expect_stat 'files in cache' 0
    expect_file_count "0" "*.manifest" $CCACHE_DIR
    expect_file_count "0" "*.o" $CCACHE_DIR
    expect_file_count "1" "*.manifest" $CCACHE_DIR_L0
    expect_file_count "1" "*.o" $CCACHE_DIR_L0
    
    # Delete .o file from L0 to simulate L0 corruption (manifest points to non-existent .o file)
    # Expect cache miss, compiler trigger and storage to L1 cache
    if [ -d $CCACHE_DIR_L0 ]; then
        find $CCACHE_DIR_L0 -type f -name '*.o' -delete 
    fi  
    CCACHE_DEPEND=1 CCACHE_DIR_L0="${CCACHE_DIR_L0}" $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3
    expect_file_count "1" "*.manifest" $CCACHE_DIR
    expect_file_count "1" "*.o" $CCACHE_DIR
    expect_file_count "1" "*.manifest" $CCACHE_DIR_L0
    expect_file_count "0" "*.o" $CCACHE_DIR_L0
    
    # Delete L0 cache - forces the file to be loaded from L1 cache
    if [ -d $CCACHE_DIR_L0 ]; then
        rm -rf $CCACHE_DIR_L0/*
    fi
    
    CCACHE_DEPEND=1 CCACHE_DIR_L0="${CCACHE_DIR_L0}" $CCACHE_COMPILE $DEPSFLAGS_CCACHE -c test.c
    expect_equal_object_files reference_test.o test.o
    expect_stat 'cache hit (direct)' 3
    expect_stat 'cache hit (preprocessed)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 3
    expect_file_count "1" "*.manifest" $CCACHE_DIR
    expect_file_count "0" "*.manifest" $CCACHE_DIR_L0

}
