if (zstd_FOUND)
  return()
endif()

if (USE_LIBZSTD_FROM_INTERNET)
  set(zstd_version ${zstd_FIND_VERSION})
  set(zstd_url https://github.com/facebook/zstd/releases/download/v${zstd_version}/zstd-${zstd_version}.tar.gz)

  set(zstd_dir ${CMAKE_BINARY_DIR}/zstd-${zstd_version})
  set(zstd_build ${CMAKE_BINARY_DIR}/zstd-build)

  file(DOWNLOAD "${zstd_url}" "${CMAKE_BINARY_DIR}/zstd.tar.gz")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/zstd.tar.gz"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

  set(ZSTD_BUILD_SHARED OFF)
  add_subdirectory("${zstd_dir}/build/cmake" "${zstd_build}" EXCLUDE_FROM_ALL)

  add_library(ZSTD::ZSTD ALIAS libzstd_static)
  set_target_properties(libzstd_static PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${zstd_dir}/lib"
  )

  set(zstd_FOUND TRUE)
else()
  find_library(ZSTD_LIBRARY zstd)
  find_path(ZSTD_INCLUDE_DIR zstd.h)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(zstd
    DEFAULT_MSG
    ZSTD_INCLUDE_DIR ZSTD_LIBRARY
  )
  mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)

  add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
  set_target_properties(ZSTD::ZSTD PROPERTIES
    IMPORTED_LOCATION "${ZSTD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
  )
endif()

include(FeatureSummary)
set_package_properties(zstd PROPERTIES
  URL "https://facebook.github.io/zstd"
  DESCRIPTION "Zstandard - Fast real-time compression algorithm")
