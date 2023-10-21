if(zstd_FOUND)
  return()
endif()

if(POLICY CMP0135)
  # Set timestamps on extracted files to time of extraction.
  cmake_policy(SET CMP0135 NEW)
endif()

set(zstd_FOUND FALSE)

if(ZSTD_FROM_INTERNET AND NOT ZSTD_FROM_INTERNET STREQUAL "AUTO")
  message(STATUS "Using zstd from the Internet")
  set(do_download TRUE)
else()
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_search_module(PC_ZSTD libzstd)
    find_library(ZSTD_LIBRARY zstd HINTS ${PC_ZSTD_LIBDIR} ${PC_ZSTD_LIBRARY_DIRS})
    find_path(ZSTD_INCLUDE_DIR zstd.h HINTS ${PC_ZSTD_INCLUDEDIR} ${PC_ZSTD_INCLUDE_DIRS})
    if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
      message(STATUS "Using zstd from ${ZSTD_LIBRARY} via pkg-config")
      set(zstd_FOUND TRUE)
    endif()
  endif()

  if(NOT zstd_FOUND)
    find_library(ZSTD_LIBRARY zstd)
    find_path(ZSTD_INCLUDE_DIR zstd.h)
    if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
      message(STATUS "Using zstd from ${ZSTD_LIBRARY}")
      set(zstd_FOUND TRUE)
    endif()
  endif()

  if(zstd_FOUND)
    mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)
    add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
    set_target_properties(
      ZSTD::ZSTD
      PROPERTIES
      IMPORTED_LOCATION "${ZSTD_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
    )
    set(zstd_FOUND TRUE)
  elseif(ZSTD_FROM_INTERNET STREQUAL "AUTO")
    message(STATUS "*** WARNING ***: Using zstd from the Internet because it was not found and ZSTD_FROM_INTERNET is AUTO")
    set(do_download TRUE)
  else()
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(
      zstd
      REQUIRED_VARS ZSTD_LIBRARY ZSTD_INCLUDE_DIR
    )
  endif()
endif()

if(do_download)
  # Although ${zstd_FIND_VERSION} was requested, let's download a newer version.
  # Note: The directory structure has changed in 1.3.0; we only support 1.3.0
  # and newer.
  set(zstd_version "1.5.5")
  set(zstd_dir   ${CMAKE_BINARY_DIR}/zstd-${zstd_version})
  set(zstd_build ${CMAKE_BINARY_DIR}/zstd-build)

  if(XCODE)
    # See https://github.com/facebook/zstd/pull/3665
    set(zstd_patch PATCH_COMMAND sed -i .bak -e s/^set_source_files_properties.*PROPERTIES.*LANGUAGE.*C/\#&/ build/cmake/lib/CMakeLists.txt)
  endif()

  include(FetchContent)
  FetchContent_Declare(
    zstd
    URL https://github.com/facebook/zstd/releases/download/v${zstd_version}/zstd-${zstd_version}.tar.gz
    URL_HASH SHA256=9c4396cc829cfae319a6e2615202e82aad41372073482fce286fac78646d3ee4
    SOURCE_DIR ${zstd_dir}
    BINARY_DIR ${zstd_build}
    ${zstd_patch}
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
  DESCRIPTION "Zstandard - Fast real-time compression algorithm"
)
