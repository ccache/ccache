if(zstd_FOUND)
  return()
endif()

set(zstd_FOUND FALSE)

find_library(ZSTD_LIBRARY zstd)
find_path(ZSTD_INCLUDE_DIR zstd.h)

if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
  mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)

  add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
  set_target_properties(
    ZSTD::ZSTD
    PROPERTIES
    IMPORTED_LOCATION "${ZSTD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}")

  set(zstd_FOUND TRUE)
elseif(ZSTD_FROM_INTERNET)
  message(STATUS "*** WARNING ***: Using zstd from the internet because it was NOT found and ZSTD_FROM_INTERNET is TRUE")

  # Although ${zstd_FIND_VERSION} was requested, let's download a newer version.
  # Note: The directory structure has changed in 1.3.0; we only support 1.3.0
  # and newer.
  set(zstd_version "1.5.2")

  set(zstd_dir   ${CMAKE_BINARY_DIR}/zstd-${zstd_version})
  set(zstd_build ${CMAKE_BINARY_DIR}/zstd-build)

  include(FetchContent)

  FetchContent_Declare(
    zstd
    URL         https://github.com/facebook/zstd/archive/v${zstd_version}.tar.gz
    URL_HASH    SHA256=f7de13462f7a82c29ab865820149e778cbfe01087b3a55b5332707abf9db4a6e
    SOURCE_DIR  ${zstd_dir}
    BINARY_DIR  ${zstd_build}
  )

  FetchContent_GetProperties(zstd)

  if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
  endif()

  set(ZSTD_BUILD_SHARED OFF)
  add_subdirectory("${zstd_dir}/build/cmake" "${zstd_build}" EXCLUDE_FROM_ALL)

  add_library(ZSTD::ZSTD ALIAS libzstd_static)
  set_target_properties(
    libzstd_static
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${zstd_dir}/lib>"
  )

  set(zstd_FOUND TRUE)
endif()

include(FeatureSummary)
set_package_properties(
  zstd
  PROPERTIES
  URL "https://facebook.github.io/zstd"
  DESCRIPTION "Zstandard - Fast real-time compression algorithm")
