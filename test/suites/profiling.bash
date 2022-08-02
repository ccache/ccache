# Remove header, including a volatile timestamp, from a .gcno file.
normalize_gcno_file() {
    local from="$1"
    local to="$2"
    tail -c +13 "${from}" >"${to}"
}


SUITE_profiling_PROBE() {
    touch test.c
    if ! $COMPILER -fprofile-generate -c test.c 2>/dev/null; then
        echo "compiler does not support profiling"
    fi
    if ! $COMPILER -fprofile-generate=data -c test.c 2>/dev/null; then
        echo "compiler does not support -fprofile-generate=path"
    fi
    if $COMPILER_TYPE_CLANG && ! command -v llvm-profdata$CLANG_VERSION_SUFFIX >/dev/null; then
        echo "llvm-profdata$CLANG_VERSION_SUFFIX tool not found"
    fi
}

SUITE_profiling_SETUP() {
    echo 'int main(void) { return 0; }' >test.c
    unset CCACHE_NODIRECT
}

SUITE_profiling() {
    # -------------------------------------------------------------------------
    TEST "-fprofile-use, missing file"

    $CCACHE_COMPILE -fprofile-use -c test.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 0
    expect_stat no_input_file 1

    # -------------------------------------------------------------------------
    TEST "-fbranch-probabilities, missing file"

    $CCACHE_COMPILE -fbranch-probabilities -c test.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 0
    expect_stat no_input_file 1

    # -------------------------------------------------------------------------
    TEST "-fprofile-use=file, missing file"

    $CCACHE_COMPILE -fprofile-use=data.gcda -c test.c 2>/dev/null
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 0
    expect_stat no_input_file 1

    # -------------------------------------------------------------------------
    TEST "-fprofile-use"

    $CCACHE_COMPILE -fprofile-generate -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -fprofile-generate test.o -o test

    ./test
    merge_profiling_data .

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    ./test
    merge_profiling_data .

    $CCACHE_COMPILE -fprofile-use -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-use=dir"

    mkdir data

    $CCACHE_COMPILE -fprofile-generate=data -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $COMPILER -fprofile-generate test.o -o test

    ./test
    merge_profiling_data data

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    ./test
    merge_profiling_data data

    $CCACHE_COMPILE -fprofile-use=data -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    # -------------------------------------------------------------------------
    TEST "-fprofile-generate=dir in different directories"

    mkdir -p dir1/data dir2/data

    cd dir1

    $CCACHE_COMPILE -Werror -fprofile-generate=data -c ../test.c \
        || test_failed "compilation error"
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -Werror -fprofile-generate=data -c ../test.c \
        || test_failed "compilation error"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    $COMPILER -Werror -fprofile-generate test.o -o test \
        || test_failed "compilation error"

    ./test || test_failed "execution error"
    merge_profiling_data data

    $CCACHE_COMPILE -Werror -fprofile-use=data -c ../test.c \
        || test_failed "compilation error"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    cd ../dir2

    $CCACHE_COMPILE -Werror -fprofile-generate=data -c ../test.c \
        || test_failed "compilation error"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 3

    $CCACHE_COMPILE -Werror -fprofile-generate=data -c ../test.c \
        || test_failed "compilation error"
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 3

    $COMPILER -Werror -fprofile-generate test.o -o test \
        || test_failed "compilation error"

    ./test || test_failed "execution error"
    merge_profiling_data data

    $CCACHE_COMPILE -Werror -fprofile-use=data -c ../test.c \
        || test_failed "compilation error"
    # Note: No expect_stat here since GCC and Clang behave differently – just
    # check that the compiler doesn't warn about not finding the profile data.

    # -------------------------------------------------------------------------
    if $COMPILER_TYPE_GCC; then
        # GCC 9 and newer creates a mangled .gcno filename (still in the current
        # working directory) if -fprofile-dir is given.
        for flag in "" -fprofile-dir=dir; do
            for dir in . subdir; do
                TEST "-ftest-coverage with -fprofile-dir=$flag, dir=$dir"
                $CCACHE -z >/dev/null

                mkdir -p "$dir"
                touch "$dir/test.c"
                find -name '*.gcno' -delete

                $COMPILER $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
                gcno_name=$(find -name '*.gcno')
                rm "$gcno_name"

                $CCACHE_COMPILE $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
                expect_stat direct_cache_hit 0
                expect_stat cache_miss 1
                expect_exists "$gcno_name"
                rm "$gcno_name"

                $CCACHE_COMPILE $flag -ftest-coverage -c $dir/test.c -o $dir/test.o
                expect_stat direct_cache_hit 1
                expect_stat cache_miss 1
                expect_exists "$gcno_name"
                rm "$gcno_name"
            done
        done
    fi

    # -------------------------------------------------------------------------
    TEST "-fprofile-arcs for different object file paths"

    mkdir obj1 obj2

    $CCACHE_COMPILE -fprofile-arcs -c test.c -o obj1/test.o
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -fprofile-arcs -c test.c -o obj1/test.o
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    $CCACHE_COMPILE -fprofile-arcs -c test.c -o obj2/test.o
    expect_different_content obj1/test.o obj2/test.o # different paths to .gcda file
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2

    $CCACHE_COMPILE -fprofile-arcs -c test.c -o obj2/test.o
    expect_different_content obj1/test.o obj2/test.o # different paths to .gcda file
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage, different directories"

    mkdir obj1 obj2

    cd obj1
    $COMPILER -ftest-coverage -c "$(pwd)/../test.c"
    normalize_gcno_file test.gcno test.gcno.reference

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    normalize_gcno_file test.gcno test.gcno.ccache-miss
    expect_equal_content test.gcno.reference test.gcno.ccache-miss

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    normalize_gcno_file test.gcno test.gcno.ccache-hit
    expect_equal_content test.gcno.reference test.gcno.ccache-hit

    cd ../obj2
    $COMPILER -ftest-coverage -c "$(pwd)/../test.c"
    normalize_gcno_file test.gcno test.gcno.reference

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    normalize_gcno_file test.gcno test.gcno.ccache-miss
    expect_equal_content test.gcno.reference test.gcno.ccache-miss

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    normalize_gcno_file test.gcno test.gcno.ccache-hit
    expect_equal_content test.gcno.reference test.gcno.ccache-hit

    # -------------------------------------------------------------------------
    TEST "-ftest-coverage, different directories, basedir, sloppy gcno_cwd"

    export CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS gcno_cwd"
    export CCACHE_BASEDIR="$(pwd)"

    mkdir obj1 obj2

    cd obj1

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1

    cd ../obj2

    $CCACHE_COMPILE -ftest-coverage -c "$(pwd)/../test.c"
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
}

merge_profiling_data() {
    local dir=$1
    if $COMPILER_TYPE_CLANG; then
        llvm-profdata$CLANG_VERSION_SUFFIX merge -output $dir/default.profdata $dir/*.profraw
    fi
}
