set(
  source_files
  argprocessing.cpp
  ccache.cpp
  compopt.cpp
  config.cpp
  context.cpp
  depfile.cpp
  execute.cpp
  hash.cpp
  hashutil.cpp
  language.cpp
  progressbar.cpp
  version.cpp
)

if(INODE_CACHE_SUPPORTED)
  list(APPEND source_files inodecache.cpp)
endif()

if(NOT WIN32)
  list(APPEND source_files signalhandler.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND source_files ${headers})

add_library(ccache_framework STATIC ${source_files})

if(WIN32)
  list(APPEND CCACHE_EXTRA_LIBS psapi)
endif()

if(CCACHE_EXTRA_LIBS)
  target_link_libraries(
    ccache_framework
    PRIVATE "${CCACHE_EXTRA_LIBS}"
  )
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(
  ccache_framework
  PUBLIC
    dep_blake3
    dep_cxxurl
    dep_fmt
    dep_nonstdspan
    dep_tlexpected
    dep_xxhash
  PRIVATE
    dep_zstd
    standard_settings
    standard_warnings
    Threads::Threads
)

if(WIN32)
  target_link_libraries(ccache_framework PUBLIC win32_compat)
endif()

get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(ccache_framework PUBLIC ${CMAKE_BINARY_DIR} ${SRC_DIR})

if(REDIS_STORAGE_BACKEND)
  target_compile_definitions(ccache_framework PUBLIC -DHAVE_REDIS_STORAGE_BACKEND)
  target_link_libraries(ccache_framework PRIVATE dep_hiredis)
endif()

if(HTTP_STORAGE_BACKEND)
  target_compile_definitions(ccache_framework PUBLIC -DHAVE_HTTP_STORAGE_BACKEND)
  target_link_libraries(ccache_framework PRIVATE dep_cpphttplib)
  if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    target_link_libraries(ccache_framework PRIVATE socket)
  endif()
endif()

add_executable(test-lockfile test_lockfile.cpp)
target_link_libraries(test-lockfile PRIVATE standard_settings standard_warnings ccache_framework)

add_subdirectory(core)
add_subdirectory(storage)
add_subdirectory(util)
