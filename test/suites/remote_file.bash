# This test suite verifies both the file storage backend and the remote
# storage framework itself.

SUITE_remote_file_SETUP() {
    unset CCACHE_NODIRECT
    remote_url=$(file_storage_url "$PWD/remote")
    export CCACHE_REMOTE_STORAGE="$remote_url helper=_builtin_"

    touch test.h
    echo '#include "test.h"' >test.c
    backdate test.h
}

SUITE_remote_file() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    # Compile and send result to local and remote storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 2 # result + manifest
    expect_stat local_storage_write 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2 # result + manifest
    expect_stat remote_storage_write 2 # result + manifest
    expect_exists remote/CACHEDIR.TAG
    subdirs=$(find remote -type d | wc -l)
    if [ "${subdirs}" -lt 2 ]; then # "remote" itself counts as one
        test_failed "Expected subdirectories in remote"
    fi
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # Get result from local storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 2 # result + manifest
    expect_stat local_storage_read_miss 2 # result + manifest
    expect_stat local_storage_write 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2
    expect_stat files_in_cache 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # Clear local storage.
    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # Get result from remote storage, copying it to local storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 2 # result + manifest
    expect_stat local_storage_read_miss 4 # 2 * (result + manifest)
    expect_stat local_storage_write 4 # 2 * (result + manifest)
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # result + manifest
    expect_stat remote_storage_read_miss 2 # result + manifest
    expect_stat remote_storage_write 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # Get result from local storage again.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 4 # 2 * (result + manifest)
    expect_stat local_storage_read_miss 4 # 2 * (result + manifest)
    expect_stat local_storage_write 4 # 2 * (result + manifest)
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # result + manifest
    expect_stat remote_storage_read_miss 2 # result + manifest
    expect_stat remote_storage_write 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Flat layout"

    CCACHE_REMOTE_STORAGE+=" @layout=flat"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_exists remote/CACHEDIR.TAG
    subdirs=$(find remote -type d | wc -l)
    if [ "${subdirs}" -ne 1 ]; then # "remote" itself counts as one
        test_failed "Expected no subdirectories in remote"
    fi
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Local layout"

    set_local_cache_file_count() {
        local files=$1
        local x
        for x in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
            mkdir -p "$CCACHE_DIR/$x"
            echo "0 0 0 0 0 0 0 0 0 0 0 $((files / 16))" >"$CCACHE_DIR/$x/stats"
        done
    }

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c

    echo 'int level_3;' >test_level_3.c
    set_local_cache_file_count $((16 * 16 * 2001))
    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test_level_3.c

    echo 'int level_4;' >test_level_4.c
    set_local_cache_file_count $((16 * 16 * 16 * 2001))
    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test_level_4.c

    cp -a "$CCACHE_DIR" remote
    $CCACHE -C >/dev/null
    $CCACHE -z >/dev/null

    CCACHE_REMOTE_STORAGE="$remote_url helper=_builtin_ @layout=local"
    $CCACHE_COMPILE -c test.c
    $CCACHE_COMPILE -c test_level_3.c
    $CCACHE_COMPILE -c test_level_4.c
    expect_stat direct_cache_hit 3
    expect_stat remote_storage_hit 3
    expect_stat remote_storage_read_hit 6 # 3 * (result + manifest)
    expect_stat remote_storage_write 0

    # A miss must not add entries to the implicitly read-only remote storage.
    remote_file_count=$(find remote -type f | wc -l)
    echo 'int x;' >>test.c
    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 0
    expect_equal $remote_file_count $(find remote -type f | wc -l)

    # -------------------------------------------------------------------------
    TEST "Local layout with read-only=false"

    CCACHE_REMOTE_STORAGE+=" @layout=local read-only=false"
    $CCACHE_COMPILE -c test.c 2>stderr.log
    expect_contains stderr.log \
        'file storage layout "local" is incompatible with read-only=false'

    # -------------------------------------------------------------------------
    TEST "Local layout with read-only=true"

    CCACHE_REMOTE_STORAGE+=" @layout=local read-only=true"
    $CCACHE_COMPILE -c test.c
    expect_stat cache_miss 1
    expect_stat remote_storage_write 0

    # -------------------------------------------------------------------------
    TEST "Invalid layout"

    for layout in '' '*' a/b; do
        CCACHE_REMOTE_STORAGE="$remote_url helper=_builtin_ @layout=$layout"
        $CCACHE_COMPILE -c test.c 2>stderr.log
        expect_contains stderr.log "invalid file storage layout: \"$layout\""
    done

    # -------------------------------------------------------------------------
    TEST "Two directories"

    remote_2_url=$(file_storage_url "$PWD/remote_2")
    CCACHE_REMOTE_STORAGE+=" $remote_2_url helper=_builtin_"
    mkdir remote_2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' remote_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' remote_2 # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' remote_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    rm -r remote/??
    expect_file_count 1 '*' remote # CACHEDIR.TAG

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote_2
    expect_file_count 1 '*' remote # CACHEDIR.TAG
    expect_file_count 3 '*' remote_2 # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    CCACHE_REMOTE_STORAGE+=" read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Depend mode"

    export CCACHE_DEPEND=1

    # Compile and send result to local and remote storage.
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 1 # only manifest
    expect_stat local_storage_write 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 1 # only manifest
    expect_stat remote_storage_write 2 # result + manifest

    # Get result from local storage.
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 2 # result + manifest
    expect_stat local_storage_read_miss 1 # manifest
    expect_stat local_storage_write 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 1
    expect_stat remote_storage_write 2

    # Clear local storage.
    $CCACHE -C >/dev/null

    # Get result from remote storage, copying it to local storage.
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 2
    expect_stat local_storage_read_miss 3
    expect_stat local_storage_write 4
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # result + manifest
    expect_stat remote_storage_read_miss 1
    expect_stat remote_storage_write 2 # result + manifest

    # Remote cache read hit for the manifest but no manifest entry matches.
    $CCACHE -C >/dev/null
    echo 'int x;' >>test.h
    backdate test.h
    $CCACHE_COMPILE -MMD -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 3
    expect_stat local_storage_read_hit 2
    expect_stat local_storage_read_miss 4 # one manifest read miss
    expect_stat local_storage_write 7 # download+store manifest, update+store manifest, write result
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 2
    expect_stat remote_storage_read_hit 3
    expect_stat remote_storage_read_miss 1 # original manifest didn't match -> no read
    expect_stat remote_storage_write 4

    if ! $HOST_OS_WINDOWS; then
        # ---------------------------------------------------------------------
        TEST "umask"

        export CCACHE_UMASK=042
        CCACHE_REMOTE_STORAGE="$remote_url helper=_builtin_ @umask=024"

        # local -> remote, cache miss
        $CCACHE_COMPILE -c test.c
        expect_perm remote drwxr-x-wx # 777 & 024
        expect_perm remote/CACHEDIR.TAG -rw-r---w- # 666 & 024
        result_file=$(find_result_files "${CCACHE_DIR}")
        expect_perm "$(dirname "${result_file}")" drwx-wxr-x # 777 & 042
        expect_perm "${result_file}" -rw--w-r-- # 666 & 042

        # local -> remote, local cache hit
        CCACHE_REMOTE_STORAGE="$remote_url helper=_builtin_ @umask=026"
        $CCACHE -C >/dev/null
        rm -rf remote
        $CCACHE_COMPILE -c test.c
        expect_perm remote drwxr-x--x # 777 & 026
        expect_perm remote/CACHEDIR.TAG -rw-r----- # 666 & 026
        result_file=$(find_result_files "${CCACHE_DIR}")
        expect_perm "$(dirname "${result_file}")" drwx-wxr-x # 777 & 042
        expect_perm "${result_file}" -rw--w-r-- # 666 & 042

        # remote -> local, remote cache hit
        $CCACHE -C >/dev/null
        $CCACHE_COMPILE -c test.c
        expect_perm remote drwxr-x--x # 777 & 026
        expect_perm remote/CACHEDIR.TAG -rw-r----- # 666 & 026
        result_file=$(find_result_files "${CCACHE_DIR}")
        expect_perm "$(dirname "${result_file}")" drwx-wxr-x # 777 & 042
        expect_perm "${result_file}" -rw--w-r-- # 666 & 042
    fi

    # -------------------------------------------------------------------------
    TEST "Sharding"

    CCACHE_REMOTE_STORAGE="$remote_url/* helper=_builtin_ shards=a,b(2)"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    if [ ! -d remote/a ] && [ ! -d remote/b ]; then
        test_failed "Expected remote/a or remote/b to exist"
    fi

    $CCACHE -Cz >/dev/null
    rm -rf remote

    remote_a_url=$(file_storage_url "$PWD/remote/a")
    remote_b_url=$(file_storage_url "$PWD/remote/b")
    CCACHE_REMOTE_STORAGE="* helper=_builtin_ shards=$remote_a_url,$remote_b_url"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    if [ ! -d remote/a ] && [ ! -d remote/b ]; then
        test_failed "Expected remote/a or remote/b to exist"
    fi

    # -------------------------------------------------------------------------
    TEST "Reshare"

    CCACHE_REMOTE_STORAGE="" $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 2
    expect_stat local_storage_write 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 0
    expect_missing remote

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 2
    expect_stat local_storage_read_miss 2
    expect_stat local_storage_write 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 0
    expect_missing remote

    CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 4
    expect_stat local_storage_read_miss 2
    expect_stat local_storage_write 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 4
    expect_stat local_storage_read_miss 4
    expect_stat local_storage_write 4
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 2
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Recache"

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 0
    expect_stat preprocessed_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 1
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 0
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 0
    expect_stat local_storage_write 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 2
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0

    expect_stat direct_cache_miss 0
    expect_stat preprocessed_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 2
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 0
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 0
    expect_stat local_storage_write 4
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 0
    expect_stat remote_storage_write 4

    # -------------------------------------------------------------------------
    if touch test.c && ln test.c test-if-fs-supports-hard-links.c 2>/dev/null; then
        TEST "Don't reshare results with raw files"

        CCACHE_REMOTE_STORAGE= CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 0
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat local_storage_hit 0
        expect_stat local_storage_miss 1
        expect_stat local_storage_read_hit 0
        expect_stat local_storage_read_miss 2
        expect_stat local_storage_write 2
        expect_stat remote_storage_hit 0
        expect_stat remote_storage_miss 0
        expect_stat remote_storage_read_hit 0
        expect_stat remote_storage_read_miss 0
        expect_stat remote_storage_write 0

        CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat local_storage_hit 1
        expect_stat local_storage_miss 1
        expect_stat local_storage_read_hit 2
        expect_stat local_storage_read_miss 2
        expect_stat local_storage_write 2
        expect_stat remote_storage_hit 0
        expect_stat remote_storage_miss 0
        expect_stat remote_storage_read_hit 0
        expect_stat remote_storage_read_miss 0
        expect_stat remote_storage_write 1 # result not saved since not self-contained
        expect_file_count 2 '*' remote # CACHEDIR.TAG + manifest, not result
    fi

    # -------------------------------------------------------------------------
    TEST "Manifest handling"

    echo 'int x;' >test.h
    backdate test.h
    echo '#include "test.h"' >test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 2 # miss: manifest + result
    expect_stat local_storage_write 2 # manifest + result
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2 # miss: manifest + result

    # Both local and remote now have an "int x;" key in the manifest.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 1 # hit: manifest without key
    expect_stat local_storage_read_miss 3 # miss: result
    expect_stat local_storage_write 5 # miss: merged manifest + new manifest entry + result
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2
    expect_stat remote_storage_read_hit 1 # # hit: manifest without key
    expect_stat remote_storage_read_miss 3 # miss: result
    expect_stat remote_storage_write 4 # miss: manifest + result

    # Both local and remote now have "int x;" and "int y;" keys in the manifest.

    $CCACHE -C >/dev/null

    # Now only remote has "int x;" and "int y;" keys in the manifest. We
    # should now be able to get remote hit without involving local.

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 3
    expect_stat local_storage_read_hit 1
    expect_stat local_storage_read_miss 5 # miss: manifest + result
    expect_stat local_storage_write 7 # miss: manifest + result
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 2
    expect_stat remote_storage_read_hit 3
    expect_stat remote_storage_read_miss 3
    expect_stat remote_storage_write 4

    # Should be able to get remote hit without involving local.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 4
    expect_stat local_storage_read_hit 2 # hit: manifest with key (downloaded from previous step)
    expect_stat local_storage_read_miss 6 # miss: manifest + result
    expect_stat local_storage_write 8 # miss: result
    expect_stat remote_storage_hit 2
    expect_stat remote_storage_miss 2
    expect_stat remote_storage_read_hit 4 # hit: result
    expect_stat remote_storage_read_miss 3
    expect_stat remote_storage_write 4

    # -------------------------------------------------------------------------
    TEST "Manifest merging"

    echo 'int x;' >test.h
    backdate test.h
    echo '#include "test.h"' >test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 2 # miss: manifest + result
    expect_stat local_storage_write 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2 # miss: manifest + result 3
    expect_stat remote_storage_write 2

    $CCACHE -C >/dev/null

    # Now remote has an "int x;" key in the manifest and local has none.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 2
    expect_stat local_storage_read_hit 0
    expect_stat local_storage_read_miss 4 # miss: manifest + result
    expect_stat local_storage_write 4
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 0
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2

    # Now local has "int y;" while remote still has "int x;".

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 3
    expect_stat local_storage_read_hit 1 # hit: manifest without key
    expect_stat local_storage_read_miss 5 # miss: result
    expect_stat local_storage_write 6
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2 # hit: manifest + result
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2

    # Local manifest with "int y;" was merged with remote's "int x;" above, so
    # we should now be able to get "int x;" and "int y;" hits locally.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 3
    expect_stat local_storage_read_hit 3 # hit: manifest + result
    expect_stat local_storage_read_miss 5 # miss: result
    expect_stat local_storage_write 6
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2

    echo 'int x;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 2
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 3
    expect_stat local_storage_read_hit 5 # hit: manifest + result
    expect_stat local_storage_read_miss 5 # miss: result
    expect_stat local_storage_write 6
    expect_stat remote_storage_hit 1
    expect_stat remote_storage_miss 1
    expect_stat remote_storage_read_hit 2
    expect_stat remote_storage_read_miss 2
    expect_stat remote_storage_write 2
}
