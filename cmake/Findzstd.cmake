if(zstd_FOUND)
  return()
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
  endif()
endif()

if(do_download)
  # Although ${zstd_FIND_VERSION} was requested, let's download a newer version.
  # Note: The directory structure has changed in 1.3.0; we only support 1.3.0
  # and newer.
  set(zstd_version "1.5.4")
  set(zstd_dir   ${CMAKE_BINARY_DIR}/zstd-${zstd_version})
  set(zstd_build ${CMAKE_BINARY_DIR}/zstd-build)

  include(FetchContent)
  FetchContent_Declare(
    zstd
    URL https://github.com/facebook/zstd/releases/download/v${zstd_version}/zstd-${zstd_version}.tar.gz
    URL_HASH SHA256=0f470992aedad543126d06efab344dc5f3e171893810455787d38347343a4424
    SOURCE_DIR ${zstd_dir}
    BINARY_DIR ${zstd_build}
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
