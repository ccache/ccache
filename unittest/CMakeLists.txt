set(
  source_files
  main.cpp
  test_argprocessing.cpp
  test_ccache.cpp
  test_compopt.cpp
  test_compression_types.cpp
  test_config.cpp
  test_core_atomicfile.cpp
  test_core_common.cpp
  test_core_msvcshowincludesoutput.cpp
  test_core_statistics.cpp
  test_core_statisticscounters.cpp
  test_core_statslog.cpp
  test_depfile.cpp
  test_hash.cpp
  test_hashutil.cpp
  test_storage_local_statsfile.cpp
  test_storage_local_util.cpp
  test_util_args.cpp
  test_util_bitset.cpp
  test_util_bytes.cpp
  test_util_clang.cpp
  test_util_conversion.cpp
  test_util_direntry.cpp
  test_util_environment.cpp
  test_util_exec.cpp
  test_util_expected.cpp
  test_util_file.cpp
  test_util_lockfile.cpp
  test_util_path.cpp
  test_util_string.cpp
  test_util_texttable.cpp
  test_util_tokenizer.cpp
  test_util_xxh3_128.cpp
  test_util_xxh3_64.cpp
  test_util_zstd.cpp
  testutil.cpp
)

if(INODE_CACHE_SUPPORTED)
  list(APPEND source_files test_inodecache.cpp)
endif()

if(WIN32)
  list(APPEND source_files test_bsdmkstemp.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND source_files ${headers})

add_executable(unittest ${source_files})

# Turn off /Zc:preprocessor for this test because it triggers a bug in some older Windows 10 SDK headers.
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
    CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19.25" AND
    CMAKE_SYSTEM_VERSION VERSION_LESS "10.0.22621")
  set_source_files_properties(test_util_DirEntry.cpp PROPERTIES COMPILE_FLAGS /Zc:preprocessor-)
endif()

target_link_libraries(
  unittest
  PRIVATE standard_settings standard_warnings ccache_framework dep_doctest
)

target_include_directories(unittest PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${ccache_SOURCE_DIR}/src)

add_test(NAME unittest COMMAND unittest)
