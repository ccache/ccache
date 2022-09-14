# This test suite verified both the file storage backend and the secondary
# storage framework itself.

SUITE_secondary_file_SETUP() {
    unset CCACHE_NODIRECT
    export CCACHE_SECONDARY_STORAGE="file:$PWD/secondary"

    generate_code 1 test.c
}

SUITE_secondary_file() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    # Compile and send result to primary and secondary storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2 # result + manifest
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 2 # result + manifest
    expect_exists secondary/CACHEDIR.TAG
    subdirs=$(find secondary -type d | wc -l)
    if [ "${subdirs}" -lt 2 ]; then # "secondary" itself counts as one
        test_failed "Expected subdirectories in secondary"
    fi
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # Get result from primary storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 2 # result + manifest
    expect_stat primary_storage_miss 2 # result + manifest
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 2 # result + manifest
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # Clear primary storage.
    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # Get result from secondary storage, copying it to primary storage.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 2
    expect_stat primary_storage_miss 4 # 2 * (result + manifest)
    expect_stat secondary_storage_hit 2 # result + manifest
    expect_stat secondary_storage_miss 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # Get result from primary storage again.
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 4
    expect_stat primary_storage_miss 4 # 2 * (result + manifest)
    expect_stat secondary_storage_hit 2 # result + manifest
    expect_stat secondary_storage_miss 2 # result + manifest
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Flat layout"

    CCACHE_SECONDARY_STORAGE+="|layout=flat"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_exists secondary/CACHEDIR.TAG
    subdirs=$(find secondary -type d | wc -l)
    if [ "${subdirs}" -ne 1 ]; then # "secondary" itself counts as one
        test_failed "Expected no subdirectories in secondary"
    fi
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Two directories"

    CCACHE_SECONDARY_STORAGE+=" file://$PWD/secondary_2"
    mkdir secondary_2

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0

    rm -r secondary/??
    expect_file_count 1 '*' secondary # CACHEDIR.TAG

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary_2
    expect_file_count 1 '*' secondary # CACHEDIR.TAG
    expect_file_count 3 '*' secondary_2 # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    CCACHE_SECONDARY_STORAGE+="|read-only"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat files_in_cache 2 # fetched from secondary
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    echo 'int x;' >> test.c
    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat files_in_cache 4
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "umask"

    export CCACHE_UMASK=042
    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary|umask=024"
    rm -rf secondary
    $CCACHE_COMPILE -c test.c
    expect_perm secondary drwxr-x-wx # 777 & 024
    expect_perm secondary/CACHEDIR.TAG -rw-r---w- # 666 & 024
    result_file=$(find $CCACHE_DIR -name '*R')
    expect_perm "$(dirname "${result_file}")" drwx-wxr-x # 777 & 042
    expect_perm "${result_file}" -rw--w-r-- # 666 & 042

    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary|umask=026"
    $CCACHE -C >/dev/null
    rm -rf secondary
    $CCACHE_COMPILE -c test.c
    expect_perm secondary drwxr-x--x # 777 & 026
    expect_perm secondary/CACHEDIR.TAG -rw-r----- # 666 & 026

    # -------------------------------------------------------------------------
    TEST "Sharding"

    CCACHE_SECONDARY_STORAGE="file://$PWD/secondary/*|shards=a,b(2)"

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    if [ ! -d secondary/a ] && [ ! -d secondary/b ]; then
        test_failed "Expected secondary/a or secondary/b to exist"
    fi

    # -------------------------------------------------------------------------
    TEST "Reshare"

    CCACHE_SECONDARY_STORAGE="" $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat files_in_cache 2
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_missing secondary

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 2
    expect_stat primary_storage_miss 2
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_missing secondary

    CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 4
    expect_stat primary_storage_miss 2
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 4
    expect_stat primary_storage_miss 4
    expect_stat secondary_storage_hit 2
    expect_stat secondary_storage_miss 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    # -------------------------------------------------------------------------
    TEST "Recache"

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 1
    expect_stat files_in_cache 2
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 1 # Try to read manifest for updating
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 1 # Try to read manifest for updating
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    $CCACHE -C >/dev/null
    expect_stat files_in_cache 0
    expect_file_count 3 '*' secondary # CACHEDIR.TAG + result + manifest

    CCACHE_RECACHE=1 $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat direct_cache_miss 0
    expect_stat cache_miss 0
    expect_stat recache 2
    expect_stat files_in_cache 2
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2 # Try to read manifest for updating
    expect_stat secondary_storage_hit 1 # Read manifest for updating
    expect_stat secondary_storage_miss 1

    # -------------------------------------------------------------------------
    if touch test.c && ln test.c test-if-fs-supports-hard-links.c 2>/dev/null; then
        TEST "Don't reshare results with raw files"

        CCACHE_SECONDARY_STORAGE= CCACHE_HARDLINK=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 0
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat primary_storage_hit 0
        expect_stat primary_storage_miss 2 # result + manifest
        expect_stat secondary_storage_hit 0
        expect_stat secondary_storage_miss 0

        CCACHE_RESHARE=1 $CCACHE_COMPILE -c test.c
        expect_stat direct_cache_hit 1
        expect_stat cache_miss 1
        expect_stat files_in_cache 3
        expect_stat primary_storage_hit 2
        expect_stat primary_storage_miss 2 # result + manifest
        expect_stat secondary_storage_hit 0
        expect_stat secondary_storage_miss 0
        expect_file_count 2 '*' secondary # CACHEDIR.TAG + manifest, not result
    fi

    # -------------------------------------------------------------------------
    TEST "Manifest handling"

    echo 'int x;' >test.h
    backdate test.h
    echo '#include "test.h"' >test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2 # miss: manifest + result
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 2 # miss: manifest + result

    # Both primary and secondary now have an "int x;" key in the manifest.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 1 # hit: manifest without key
    expect_stat primary_storage_miss 3 # miss: result
    expect_stat secondary_storage_hit 1 # his: manifest without key
    expect_stat secondary_storage_miss 3 # miss: result

    # Both primary and secondary now have "int x;" and "int y;" keys in the manifest.

    $CCACHE -C >/dev/null

    # Now only secondary has "int x;" and "int y;" keys in the manifest. We
    # should now be able to get secondary hit without involving primary.

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 1
    expect_stat primary_storage_miss 5 # miss: manifest + result
    expect_stat secondary_storage_hit 3 # hit: manifest + result
    expect_stat secondary_storage_miss 3

    # Should be able to get secondary hit without involving primary.

    echo 'int y;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 2 # hit: manifest with key (downloaded from previous step)
    expect_stat primary_storage_miss 6 # miss: result
    expect_stat secondary_storage_hit 4 # hit: result
    expect_stat secondary_storage_miss 3

    # -------------------------------------------------------------------------
    TEST "Manifest merging"

    echo 'int x;' >test.h
    backdate test.h
    echo '#include "test.h"' >test.c

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 1
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 2 # miss: manifest + result
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 2 # miss: manifest + result

    $CCACHE -C >/dev/null

    # Now secondary has an "int x;" key in the manifest and primary has none.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_SECONDARY_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 0
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 0
    expect_stat primary_storage_miss 4 # miss: manifest + result
    expect_stat secondary_storage_hit 0
    expect_stat secondary_storage_miss 2

    # Now primary has "int y;" while secondary still has "int x;".

    echo 'int x;' >test.h
    backdate test.h

    $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 1
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 1 # hit: manifest without key
    expect_stat primary_storage_miss 5 # miss: result
    expect_stat secondary_storage_hit 2 # hit: manifest + result
    expect_stat secondary_storage_miss 2

    # Primary's manifest with "int y;" was merged with secondary's "int x;"
    # above, so we should now be able to get "int x;" and "int y;" hits locally.

    echo 'int y;' >test.h
    backdate test.h

    CCACHE_SECONDARY_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 2
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 3 # hit: manifest + result
    expect_stat primary_storage_miss 5
    expect_stat secondary_storage_hit 2
    expect_stat secondary_storage_miss 2

    echo 'int x;' >test.h
    backdate test.h

    CCACHE_SECONDARY_STORAGE= $CCACHE_COMPILE -c test.c
    expect_stat direct_cache_hit 3
    expect_stat cache_miss 2
    expect_stat primary_storage_hit 5 # hit: manifest + result
    expect_stat primary_storage_miss 5
    expect_stat secondary_storage_hit 2
    expect_stat secondary_storage_miss 2
}
