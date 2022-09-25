# This test suite verified both the file storage backend and the remote
# storage framework itself.

SUITE_remote_file_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_REMOTE_STORAGE="file:$PWD/remote"

    generate_code 1 test.c
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
    expect_stat local_storage_miss 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2 # result + manifest
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
    expect_stat local_storage_hit 2 # result + manifest
    expect_stat local_storage_miss 2 # result + manifest
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2 # result + manifest
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
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 4 # 2 * (result + manifest)
    expect_stat remote_storage_hit 2 # result + manifest
    expect_stat remote_storage_miss 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # Get result from local storage again.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat local_storage_hit 4
    expect_stat local_storage_miss 4 # 2 * (result + manifest)
    expect_stat remote_storage_hit 2 # result + manifest
    expect_stat remote_storage_miss 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from remote
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Flat layout"

    CCACHE_REMOTE_STORAGE+="|layout=flat"

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
    TEST "Two directories"

    CCACHE_REMOTE_STORAGE+=" file://$PWD/remote_2"
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

    CCACHE_REMOTE_STORAGE+="|read-only"

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
    TEST "umask"

    export CCACHE_UMASK=042
    CCACHE_REMOTE_STORAGE="file://$PWD/remote|umask=024"
    rm -rf remote
    $CCACHE_COMPILE -c test.c
    expect_perm remote drwxr-x-wx # 777 & 024
    expect_perm remote/CACHEDIR.TAG -rw-r---w- # 666 & 024
    result_file=$(find $CCACHE_DIR -name '*R')
    expect_perm "$(dirname "${result_file}")" drwx-wxr-x # 777 & 042
    expect_perm "${result_file}" -rw--w-r-- # 666 & 042

    CCACHE_REMOTE_STORAGE="file://$PWD/remote|umask=026"
    $CCACHE -C >/dev/null
    rm -rf remote
    $CCACHE_COMPILE -c test.c
    expect_perm remote drwxr-x--x # 777 & 026
    expect_perm remote/CACHEDIR.TAG -rw-r----- # 666 & 026

    # -------------------------------------------------------------------------
    TEST "Sharding"

    CCACHE_REMOTE_STORAGE="file://$PWD/remote/*|shards=a,b(2)"

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
    expect_stat local_storage_miss 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_missing remote

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat local_storage_hit 2
    expect_stat local_storage_miss 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_missing remote

    CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat local_storage_hit 4
    expect_stat local_storage_miss 2
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat local_storage_hit 4
    expect_stat local_storage_miss 4
    expect_stat remote_storage_hit 2
    expect_stat remote_storage_miss 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Recache"

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 1
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 1 # Try to read manifest for updating
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 1 # Try to read manifest for updating
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' remote # CACHEDIR.TAG + result + manifest

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 2
    expect_stat files_in_cache 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 2 # Try to read manifest for updating
    expect_stat remote_storage_hit 1 # Read manifest for updating
    expect_stat remote_storage_miss 1

    # -------------------------------------------------------------------------
    if touch test.c && ln test.c test-if-fs-supports-hard-links.c 2>/dev/null; then
        TEST "Don't reshare results with raw files"

        CCACHE_REMOTE_STORAGE= CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 0
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat local_storage_hit 0
        expect_stat local_storage_miss 2 # result + manifest
        expect_stat remote_storage_hit 0
        expect_stat remote_storage_miss 0

        CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat local_storage_hit 2
        expect_stat local_storage_miss 2 # result + manifest
        expect_stat remote_storage_hit 0
        expect_stat remote_storage_miss 0
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
    expect_stat local_storage_miss 2 # miss: manifest + result
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2 # miss: manifest + result

    # Both local and remote now have an "int x;" key in the manifest.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat local_storage_hit 1 # hit: manifest without key
    expect_stat local_storage_miss 3 # miss: result
    expect_stat remote_storage_hit 1 # his: manifest without key
    expect_stat remote_storage_miss 3 # miss: result

    # Both local and remote now have "int x;" and "int y;" keys in the manifest.

    $CCACHE -C >/dev/null

    # Now only remote has "int x;" and "int y;" keys in the manifest. We
    # should now be able to get remote hit without involving local.

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat local_storage_hit 1
    expect_stat local_storage_miss 5 # miss: manifest + result
    expect_stat remote_storage_hit 3 # hit: manifest + result
    expect_stat remote_storage_miss 3

    # Should be able to get remote hit without involving local.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat local_storage_hit 2 # hit: manifest with key (downloaded from previous step)
    expect_stat local_storage_miss 6 # miss: result
    expect_stat remote_storage_hit 4 # hit: result
    expect_stat remote_storage_miss 3

    # -------------------------------------------------------------------------
    TEST "Manifest merging"

    echo 'int x;' >test.h
    backdate test.h
    echo '#include "test.h"' >test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 2 # miss: manifest + result
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2 # miss: manifest + result

    $CCACHE -C >/dev/null

    # Now remote has an "int x;" key in the manifest and local has none.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat local_storage_hit 0
    expect_stat local_storage_miss 4 # miss: manifest + result
    expect_stat remote_storage_hit 0
    expect_stat remote_storage_miss 2

    # Now local has "int y;" while remote still has "int x;".

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat local_storage_hit 1 # hit: manifest without key
    expect_stat local_storage_miss 5 # miss: result
    expect_stat remote_storage_hit 2 # hit: manifest + result
    expect_stat remote_storage_miss 2

    # Local manifest with "int y;" was merged with remote's "int x;" above, so
    # we should now be able to get "int x;" and "int y;" hits locally.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat local_storage_hit 3 # hit: manifest + result
    expect_stat local_storage_miss 5
    expect_stat remote_storage_hit 2
    expect_stat remote_storage_miss 2

    echo 'int x;' >test.h
    backdate test.h

    CCACHE_REMOTE_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 2
    expect_stat local_storage_hit 5 # hit: manifest + result
    expect_stat local_storage_miss 5
    expect_stat remote_storage_hit 2
    expect_stat remote_storage_miss 2
}
