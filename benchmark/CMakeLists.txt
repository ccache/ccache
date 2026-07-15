include(FetchContent)

set(_benchmark_version_string 1.9.5)
message(STATUS "Downloading Google Benchmark ${_benchmark_version_string}")

set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  benchmark
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL "https://github.com/google/benchmark/archive/refs/tags/v${_benchmark_version_string}.tar.gz"
  URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
)
FetchContent_MakeAvailable(benchmark)

register_dependency(GoogleBenchmark DOWNLOADED "${_benchmark_version_string}")

set(
  source_files
  main.cpp
  benchmark_hashutil.cpp
)

add_executable(ccache_benchmark ${source_files})

target_link_libraries(
  ccache_benchmark
  PRIVATE standard_settings standard_warnings ccache_framework benchmark::benchmark
)

target_include_directories(
  ccache_benchmark
  PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${ccache_SOURCE_DIR}/src
)

add_custom_target(
  benchmarks
  COMMAND ccache_benchmark
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS ccache_benchmark
  COMMENT "Running benchmarks"
  USES_TERMINAL
)
