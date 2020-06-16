add_executable(
  unittest
  TestUtil.cpp
  catch2_tests.cpp
  main.cpp
  test_Args.cpp
  test_AtomicFile.cpp
  test_Checksum.cpp
  test_Compression.cpp
  test_Config.cpp
  test_FormatNonstdStringView.cpp
  test_InodeCache.cpp
  test_Lockfile.cpp
  test_NullCompression.cpp
  test_Stat.cpp
  test_Util.cpp
  test_ZstdCompression.cpp
  test_argprocessing.cpp
  test_compopt.cpp
  test_hash.cpp
  test_hashutil.cpp
  test_legacy_util.cpp)

target_link_libraries(
  unittest
  PRIVATE standard_settings standard_warnings ccache_lib)

target_include_directories(unittest PRIVATE ${CMAKE_BINARY_DIR} . ../src)

add_test(NAME unittest COMMAND unittest)
