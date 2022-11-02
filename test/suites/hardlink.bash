SUITE_hardlink_PROBE() {
    # Probe hard link across directories since AFS doesn't support those.
    mkdir dir
    touch dir/file1
    if ! ln dir/file1 file2 >/dev/null 2>&1; then
        echo "file system doesn't support hardlinks"
    fi
}

SUITE_hardlink() {
    # -------------------------------------------------------------------------
    TEST "CCACHE_HARDLINK"

    generate_code 1 test1.c

    $COMPILER -c -o reference_test1.o test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_equal_object_files reference_test1.o test1.o

    mv test1.o test1.o.saved

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    if [ ! test1.o -ef test1.o.saved ]; then
        test_failed "Object files not hard linked"
    fi

    $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    if [ test1.o -ef test1.o.saved ]; then
        test_failed "Object files are hard linked"
    fi

    # -------------------------------------------------------------------------
    TEST "Corrupted file size is detected"

    generate_code 1 test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    mv test1.o test1.o.saved

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.c
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    # -------------------------------------------------------------------------
if $RUN_WIN_XFAIL; then
    TEST "Overwrite assembler"

    generate_code 1 test1.c
    $COMPILER -S -o test1.s test1.c

    $COMPILER -c -o reference_test1.o test1.s

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2

    generate_code 2 test1.c
    $COMPILER -S -o test1.s test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat preprocessed_cache_hit 0
    expect_stat cache_miss 2
    expect_stat files_in_cache 4

    generate_code 1 test1.c
    $COMPILER -S -o test1.s test1.c

    CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test1.s
    expect_stat preprocessed_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_equal_object_files reference_test1.o test1.o
fi
    # -------------------------------------------------------------------------
if $RUN_WIN_XFAIL; then
    TEST "Automake depend move"

    unset CCACHE_NODIRECT

    generate_code 1 test1.c

    CCACHE_HARDLINK=1 CCACHE_DEPEND=1 $CCACHE_COMPILE -c -MMD -MF test1.d.tmp test1.c
    expect_stat direct_cache_hit 0
    mv test1.d.tmp test1.d || test_failed "first mv failed"

    CCACHE_HARDLINK=1 CCACHE_DEPEND=1 $CCACHE_COMPILE -c -MMD -MF test1.d.tmp test1.c
    expect_stat direct_cache_hit 1
    mv test1.d.tmp test1.d || test_failed "second mv failed"
fi
    # -------------------------------------------------------------------------
if $RUN_WIN_XFAIL; then
    TEST ".d file corrupted by compiler"

    unset CCACHE_NODIRECT
    export CCACHE_SLOPPINESS=include_file_mtime,include_file_ctime
    export CCACHE_HARDLINK=1

    echo "int x;" >test1.c

    $CCACHE_COMPILE -c -MMD test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_content test1.d "test1.o: test1.c"

    touch test1.h
    echo '#include "test1.h"' >>test1.c

    $CCACHE_COMPILE -c -MMD test1.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_content test1.d "test1.o: test1.c test1.h"

    echo "int x;" >test1.c

    $CCACHE_COMPILE -c -MMD test1.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_content test1.d "test1.o: test1.c"
fi
}
