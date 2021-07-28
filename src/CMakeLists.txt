set(
  source_files
  Args.cpp
  AtomicFile.cpp
  CacheEntryReader.cpp
  CacheEntryWriter.cpp
  Config.cpp
  Context.cpp
  Depfile.cpp
  Fd.cpp
  Hash.cpp
  Lockfile.cpp
  Logging.cpp
  Manifest.cpp
  ProgressBar.cpp
  Result.cpp
  ResultDumper.cpp
  ResultExtractor.cpp
  ResultRetriever.cpp
  SignalHandler.cpp
  Stat.cpp
  TemporaryFile.cpp
  ThreadPool.cpp
  Util.cpp
  argprocessing.cpp
  assertions.cpp
  ccache.cpp
  compopt.cpp
  execute.cpp
  hashutil.cpp
  language.cpp
  version.cpp
)

if(INODE_CACHE_SUPPORTED)
  list(APPEND source_files InodeCache.cpp)
endif()

if(MTR_ENABLED)
  list(APPEND source_files MiniTrace.cpp)
endif()

if(WIN32)
  list(APPEND source_files Win32Util.cpp)
endif()

add_library(ccache_framework STATIC ${source_files})
target_compile_definitions(
  ccache_framework PUBLIC -Dnssv_CONFIG_SELECT_STRING_VIEW=nssv_STRING_VIEW_NONSTD
)

if(WIN32)
  target_link_libraries(ccache_framework PRIVATE ws2_32 "psapi")

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if(STATIC_LINK)
      target_link_libraries(ccache_framework PRIVATE -static-libgcc -static-libstdc++ -static winpthread -dynamic)
    else()
      target_link_libraries(ccache_framework PRIVATE winpthread)
    endif()
  elseif(STATIC_LINK AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_libraries(ccache_framework PRIVATE -static c++ -dynamic)
  endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(
  ccache_framework
  PRIVATE standard_settings standard_warnings ZSTD::ZSTD Threads::Threads third_party
)

target_include_directories(ccache_framework PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

if(REDIS_STORAGE_BACKEND)
  target_compile_definitions(ccache_framework PRIVATE -DHAVE_REDIS_STORAGE_BACKEND)
  target_link_libraries(
    ccache_framework
    PUBLIC standard_settings standard_warnings HIREDIS::HIREDIS third_party
  )
endif()

add_subdirectory(compression)
add_subdirectory(core)
add_subdirectory(storage)
add_subdirectory(third_party)
add_subdirectory(util)
