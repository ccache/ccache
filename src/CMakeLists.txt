set(
  source_files
  Args.cpp
  CacheEntryReader.cpp
  CacheEntryWriter.cpp
  CacheFile.cpp
  Context.cpp
  Depfile.cpp
  Hash.cpp
  Manifest.cpp
  MiniTrace.cpp
  ProgressBar.cpp
  Result.cpp
  ResultDumper.cpp
  ResultExtractor.cpp
  ResultRetriever.cpp
  SignalHandler.cpp
  ThreadPool.cpp
  Util.cpp
  argprocessing.cpp
  ccache.cpp
  cleanup.cpp
  compopt.cpp
  compress.cpp
  execute.cpp
  hashutil.cpp
  language.cpp
  version.cpp)

if(INODE_CACHE_SUPPORTED)
  list(APPEND source_files InodeCache.cpp)
endif()

if(WIN32)
  list(APPEND source_files Win32Util.cpp)
endif()

add_library(ccache_lib STATIC ${source_files})

if(WIN32)
  target_link_libraries(ccache_lib PRIVATE ws2_32 "psapi")

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if(STATIC_LINK)
      target_link_libraries(ccache_lib PRIVATE -static-libgcc -static-libstdc++ -static winpthread -dynamic)
    else()
      target_link_libraries(ccache_lib PRIVATE winpthread)
    endif()
  elseif(STATIC_LINK AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_libraries(ccache_lib PRIVATE -static c++ -dynamic)
  endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(
  ccache_lib
  PRIVATE standard_settings standard_warnings ZSTD::ZSTD
          Threads::Threads third_party_lib)

target_link_libraries(ccache_lib PUBLIC ccache_core ccache_compression)

add_subdirectory(third_party)
add_subdirectory(core)
add_subdirectory(compression)
