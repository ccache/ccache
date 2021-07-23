set(
  source_files
  TestUtil.cpp
  main.cpp
  test_Args.cpp
  test_AtomicFile.cpp
  test_Checksum.cpp
  test_Config.cpp
  test_Depfile.cpp
  test_FormatNonstdStringView.cpp
  test_Hash.cpp
  test_Lockfile.cpp
  test_NullCompression.cpp
  test_Stat.cpp
  test_Util.cpp
  test_ZstdCompression.cpp
  test_argprocessing.cpp
  test_ccache.cpp
  test_compopt.cpp
  test_compression_types.cpp
  test_core_Statistics.cpp
  test_core_StatisticsCounters.cpp
  test_core_StatsLog.cpp
  test_hashutil.cpp
  test_storage_primary_StatsFile.cpp
  test_storage_primary_util.cpp
  test_util_TextTable.cpp
  test_util_Tokenizer.cpp
  test_util_expected.cpp
  test_util_path.cpp
  test_util_string.cpp
)

if(INODE_CACHE_SUPPORTED)
  list(APPEND source_files test_InodeCache.cpp)
endif()

if(WIN32)
  list(APPEND source_files test_bsdmkstemp.cpp test_Win32Util.cpp)
endif()

add_executable(unittest ${source_files})

target_link_libraries(
  unittest
  PRIVATE standard_settings standard_warnings ccache_lib third_party_lib)

target_include_directories(unittest PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${ccache_SOURCE_DIR}/src)

add_test(NAME unittest COMMAND unittest)
