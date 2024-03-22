set(
  source_files
  Args.cpp
  Config.cpp
  Context.cpp
  Depfile.cpp
  Hash.cpp
  ProgressBar.cpp
  Util.cpp
  argprocessing.cpp
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

if(NOT WIN32)
  list(APPEND source_files SignalHandler.cpp)
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
    dep_fmt
    dep_nonstdspan
    third_party
  PRIVATE
    dep_cpphttplib
    dep_zstd
    standard_settings
    standard_warnings
    Threads::Threads
)

get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(ccache_framework PUBLIC ${CMAKE_BINARY_DIR} ${SRC_DIR})

if(REDIS_STORAGE_BACKEND)
  target_compile_definitions(ccache_framework PUBLIC -DHAVE_REDIS_STORAGE_BACKEND)
  target_link_libraries(ccache_framework PRIVATE dep_hiredis)
endif()

add_executable(test-lockfile test_lockfile.cpp)
target_link_libraries(test-lockfile PRIVATE standard_settings standard_warnings ccache_framework)

add_subdirectory(core)
add_subdirectory(storage)
add_subdirectory(util)
