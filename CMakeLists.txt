cmake_minimum_required(VERSION 3.5)

project(ccache)
set(CMAKE_CXX_STANDARD 11)

#
# 3rd party
#
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(USE_LIBZSTD_FROM_INTERNET "Download and use libzstd from the Internet" ON)
find_package(zstd 1.4.4 REQUIRED)

option(USE_LIBB2_FROM_INTERNET "Download and use libb2 from the Internet" ON)
find_package(libb2 0.98.1 REQUIRED)

#
# ccache
#

set(ccache_sources
  src/args.cpp src/args.hpp
  src/ArgsInfo.cpp src/ArgsInfo.hpp
  src/AtomicFile.cpp src/AtomicFile.hpp
  src/CacheEntryReader.cpp src/CacheEntryReader.hpp
  src/CacheEntryWriter.cpp src/CacheEntryWriter.hpp
  src/CacheFile.cpp src/CacheFile.hpp
  src/ccache.cpp src/ccache.hpp
  src/Checksum.hpp
  src/cleanup.cpp src/cleanup.hpp
  src/compopt.cpp src/compopt.hpp
  src/compress.cpp src/compress.hpp
  src/Compression.cpp src/Compression.hpp
  src/Compressor.cpp src/Compressor.hpp
  src/Config.cpp src/Config.hpp
  src/counters.cpp src/counters.hpp
  src/Decompressor.cpp src/Decompressor.hpp
  src/Error.hpp
  src/execute.cpp src/execute.hpp
  src/exitfn.cpp src/exitfn.hpp
  src/File.hpp
  src/FormatNonstdStringView.hpp
  src/hash.cpp src/hash.hpp
  src/hashutil.cpp src/hashutil.hpp
  src/language.cpp src/language.hpp
  src/legacy_globals.cpp src/legacy_globals.hpp
  src/legacy_util.cpp src/legacy_util.hpp
  src/lockfile.cpp src/lockfile.hpp
  src/macroskip.hpp
  src/manifest.cpp src/manifest.hpp
  src/NonCopyable.hpp
  src/NullCompressor.cpp src/NullCompressor.hpp
  src/NullDecompressor.cpp src/NullDecompressor.hpp
  src/ProgressBar.cpp src/ProgressBar.hpp
  src/result.cpp src/result.hpp
  src/Stat.cpp src/Stat.hpp
  src/stats.cpp src/stats.hpp
  src/StdMakeUnique.hpp
  src/system.hpp
  src/ThreadPool.hpp
  src/Util.cpp src/Util.hpp
  src/ZstdCompressor.cpp src/ZstdCompressor.hpp
  src/ZstdDecompressor.cpp src/ZstdDecompressor.hpp

  src/third_party/fmt/core.h
  src/third_party/fmt/format-inl.h
  src/third_party/fmt/format.h
  src/third_party/getopt_long.h
  src/third_party/minitrace.h
  src/third_party/nonstd/optional.hpp
  src/third_party/nonstd/string_view.hpp
  src/third_party/xxhash.h

  src/third_party/format.cpp
  src/third_party/getopt_long.c
  src/third_party/minitrace.c
  src/third_party/xxhash.c
)

find_package(Git)
execute_process(COMMAND ${GIT_EXECUTABLE} describe
  OUTPUT_VARIABLE git_output
  ERROR_VARIABLE git_error
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if (NOT git_error)
  string(REGEX MATCH "[0-9.]+" CCACHE_VERSION ${git_output})
else()
  set(CCACHE_VERSION 3.7.7)
endif()

file(WRITE ${CMAKE_BINARY_DIR}/version.cpp.in [=[
  extern const char CCACHE_VERSION[];
  const char CCACHE_VERSION[] = "@CCACHE_VERSION@";
]=])
configure_file(
  ${CMAKE_BINARY_DIR}/version.cpp.in
  ${CMAKE_BINARY_DIR}/version.cpp @ONLY
)

list(APPEND ccache_sources ${CMAKE_BINARY_DIR}/version.cpp)

add_library(ccache_lib STATIC ${ccache_sources})
target_include_directories(ccache_lib
  PUBLIC ${CMAKE_BINARY_DIR} . src src/third_party)
target_link_libraries(ccache_lib
  PUBLIC ZSTD::ZSTD libb2::libb2)

add_executable(ccache src/main.cpp)
target_link_libraries(ccache PRIVATE ccache_lib)

install(TARGETS ccache DESTINATION .)

#
# unittest
#

enable_testing()
set(unittest_files
  unittest/catch2_tests.cpp unittest/catch2_tests.hpp
  unittest/framework.cpp unittest/framework.hpp
  unittest/main.cpp
  unittest/test_args.cpp
  unittest/test_argument_processing.cpp
  unittest/test_AtomicFile.cpp
  unittest/test_Checksum.cpp
  unittest/test_compopt.cpp
  unittest/test_Compression.cpp
  unittest/test_Config.cpp
  unittest/test_counters.cpp
  unittest/test_FormatNonstdStringView.cpp
  unittest/test_hash.cpp
  unittest/test_hashutil.cpp
  unittest/test_legacy_util.cpp
  unittest/test_lockfile.cpp
  unittest/test_NullCompression.cpp
  unittest/test_Stat.cpp
  unittest/test_stats.cpp
  unittest/test_Util.cpp
  unittest/test_ZstdCompression.cpp
  unittest/util.cpp unittest/util.hpp
)

add_executable(unittest ${unittest_files})
target_link_libraries(unittest PRIVATE ccache_lib)

add_test(NAME unittest COMMAND unittest)

#
# Configuration
#

include(CheckIncludeFile)
foreach(include_file IN ITEMS
    pwd.h
    sys/mman.h
    sys/time.h
    sys/wait.h
    termios.h
  )
  string(TOUPPER ${include_file} include_var)
  string(REGEX REPLACE "[/.]" "_" include_var ${include_var})
  set(include_var HAVE_${include_var})
  check_include_file(${include_file} ${include_var})
endforeach()

include(CheckFunctionExists)
foreach(func IN ITEMS
    GetFinalPathNameByHandleW
    getopt_long
    getpwuid
    gettimeofday
    localtime_r
    mkstemp
    realpath
    strndup
    strtok_r
    unsetenv
    utimes
  )
  string(TOUPPER ${func} func_var)
  set(func_var HAVE_${func_var})
  check_function_exists(${func} ${func_var})
endforeach()

include(CheckSymbolExists)
list(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)
check_symbol_exists(gethostname winsock2.h HAVE_GETHOSTNAME)
list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ws2_32)

include(CheckTypeSize)
check_type_size("long long" HAVE_LONG_LONG)

if(WIN32)
  set(_WIN32_WINNT 0x0600)
  target_link_libraries(ccache PRIVATE ws2_32)
  target_link_libraries(unittest PRIVATE ws2_32)

  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_libraries (ccache PRIVATE -static gcc stdc++ winpthread -dynamic)
  else()
    target_link_libraries (ccache PRIVATE -static c++ -dynamic)
  endif()
endif()

set(_GNU_SOURCE 1)
set(_POSIX_C_SOURCE 1)

if (CMAKE_SYSTEM MATCHES "Darwin")
  set(_DARWIN_C_SOURCE 1)
endif()

configure_file(config.h.cmake.in ${CMAKE_BINARY_DIR}/config.h)
