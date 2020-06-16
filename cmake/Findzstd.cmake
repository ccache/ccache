if(zstd_FOUND)
  return()
endif()

if(USE_LIBZSTD_FROM_INTERNET)
  # Although ${zstd_FIND_VERSION} was requested, let's download a newer version.
  # Note: The directory structure has changed in 1.3.0; we only support 1.3.0
  # and newer.
  set(zstd_version "1.4.4")
  set(zstd_url https://github.com/facebook/zstd/archive/v${zstd_version}.tar.gz)

  set(zstd_dir ${CMAKE_BINARY_DIR}/zstd-${zstd_version})
  set(zstd_build ${CMAKE_BINARY_DIR}/zstd-build)

  if(NOT EXISTS "${zstd_dir}.tar.gz")
    file(DOWNLOAD "${zstd_url}" "${zstd_dir}.tar.gz")
  endif()

  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${zstd_dir}.tar.gz"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

  set(ZSTD_BUILD_SHARED OFF)
  add_subdirectory("${zstd_dir}/build/cmake" "${zstd_build}" EXCLUDE_FROM_ALL)

  add_library(ZSTD::ZSTD ALIAS libzstd_static)
  set_target_properties(
    libzstd_static
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${zstd_dir}/lib")

  set(zstd_FOUND TRUE)
else()
  find_library(ZSTD_LIBRARY zstd)
  find_path(ZSTD_INCLUDE_DIR zstd.h)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    zstd "please install libzstd or use -DUSE_LIBZSTD_FROM_INTERNET=ON"
    ZSTD_INCLUDE_DIR ZSTD_LIBRARY)
  mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)

  add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
  set_target_properties(
    ZSTD::ZSTD
    PROPERTIES
    IMPORTED_LOCATION "${ZSTD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}")
endif()

include(FeatureSummary)
set_package_properties(
  zstd
  PROPERTIES
  URL "https://facebook.github.io/zstd"
  DESCRIPTION "Zstandard - Fast real-time compression algorithm")
