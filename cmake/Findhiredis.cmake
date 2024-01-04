if(hiredis_FOUND)
  return()
endif()

if(POLICY CMP0135)
  # Set timestamps on extracted files to time of extraction.
  cmake_policy(SET CMP0135 NEW)
endif()

set(hiredis_FOUND FALSE)
set(hiredis_ssl_FOUND FALSE)

if(HIREDIS_FROM_INTERNET AND NOT HIREDIS_FROM_INTERNET STREQUAL "AUTO")
  message(STATUS "Using hiredis from the Internet")
  set(do_download TRUE)
else()
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(HIREDIS hiredis>=${hiredis_FIND_VERSION})
    find_library(HIREDIS_LIBRARY ${HIREDIS_LIBRARIES} HINTS ${HIREDIS_LIBDIR})
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ${HIREDIS_PREFIX}/include)
    if(HIREDIS_LIBRARY AND HIREDIS_INCLUDE_DIR)
      message(STATUS "Using hiredis from ${HIREDIS_LIBRARY} via pkg-config")
      set(hiredis_FOUND TRUE)
    endif()
    pkg_check_modules(HIREDIS_SSL hiredis_ssl>=${hiredis_FIND_VERSION})
    find_library(HIREDIS_SSL_LIBRARY ${HIREDIS_SSL_LIBRARIES} HINTS ${HIREDIS_SSL_LIBDIR})
    find_path(HIREDIS_SSL_INCLUDE_DIR hiredis/hiredis_ssl.h HINTS ${HIREDIS_SSL_PREFIX}/include)
    if(HIREDIS_SSL_LIBRARY AND HIREDIS_SSL_INCLUDE_DIR)
      message(STATUS "Using hiredis_ssl from ${HIREDIS_SSL_LIBRARY} via pkg-config")
      set(hiredis_ssl_FOUND TRUE)
    endif()
  endif()

  if(NOT hiredis_FOUND)
    find_library(HIREDIS_LIBRARY hiredis)
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
    if(HIREDIS_LIBRARY AND HIREDIS_INCLUDE_DIR)
      message(STATUS "Using hiredis from ${HIREDIS_LIBRARY}")
      set(hiredis_FOUND TRUE)
    endif()
    find_library(HIREDIS_SSL_LIBRARY hiredis_ssl)
    find_path(HIREDIS_SSL_INCLUDE_DIR hiredis/hiredis_ssl.h)
    if(HIREDIS_SSL_LIBRARY AND HIREDIS_SSL_INCLUDE_DIR)
      message(STATUS "Using hiredis_ssl from ${HIREDIS_SSL_LIBRARY} via pkg-config")
      set(hiredis_ssl_FOUND TRUE)
    endif()
  endif()

  if(hiredis_FOUND)
    mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)
    add_library(HIREDIS::HIREDIS UNKNOWN IMPORTED)
    set_target_properties(
      HIREDIS::HIREDIS
      PROPERTIES
      IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
      INTERFACE_COMPILE_OPTIONS "${HIREDIS_CFLAGS_OTHER}"
      INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
    )
    if(WIN32 AND STATIC_LINK)
      target_link_libraries(HIREDIS::HIREDIS INTERFACE ws2_32)
    endif()
    set(hiredis_FOUND TRUE)
    set(target HIREDIS::HIREDIS)
  elseif(HIREDIS_FROM_INTERNET STREQUAL "AUTO")
    message(STATUS "*** WARNING ***: Using hiredis from the Internet because it was not found and HIREDIS_FROM_INTERNET is AUTO")
    set(do_download TRUE)
  else()
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(
      hiredis
      REQUIRED_VARS HIREDIS_LIBRARY HIREDIS_INCLUDE_DIR
    )
  endif()
endif()

if(do_download)
  set(hiredis_version "1.2.0")
  set(hiredis_dir   ${CMAKE_BINARY_DIR}/hiredis-${hiredis_version})
  set(hiredis_build ${CMAKE_BINARY_DIR}/hiredis-build)

  include(FetchContent)
  FetchContent_Declare(
    hiredis
    URL https://github.com/redis/hiredis/archive/refs/tags/v${hiredis_version}.tar.gz
    SOURCE_DIR ${hiredis_dir}
    BINARY_DIR ${hiredis_build}
  )

  FetchContent_GetProperties(hiredis)
  if(NOT hiredis_POPULATED)
    FetchContent_Populate(hiredis)
  endif()

  find_package(OpenSSL)

  set(
    hiredis_sources
    "${hiredis_dir}/alloc.c"
    "${hiredis_dir}/async.c"
    "${hiredis_dir}/dict.c"
    "${hiredis_dir}/hiredis.c"
    "${hiredis_dir}/net.c"
    "${hiredis_dir}/read.c"
    "${hiredis_dir}/sds.c"
    "${hiredis_dir}/sockcompat.c"
  )
  set(
    hiredis_ssl_sources
    "${hiredis_dir}/ssl.c"
  )
  add_library(libhiredis_static STATIC EXCLUDE_FROM_ALL ${hiredis_sources})
  add_library(HIREDIS::HIREDIS ALIAS libhiredis_static)
  add_library(libhiredis_ssl_static STATIC EXCLUDE_FROM_ALL ${hiredis_ssl_sources})
  add_library(HIREDIS::HIREDIS_SSL ALIAS libhiredis_ssl_static)

  if(WIN32)
    target_compile_definitions(libhiredis_static PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  make_directory("${hiredis_dir}/include")
  make_directory("${hiredis_dir}/include/hiredis")
  file(GLOB hiredis_headers "${hiredis_dir}/*.h")
  file(COPY ${hiredis_headers} DESTINATION "${hiredis_dir}/include/hiredis")
  set_target_properties(
    libhiredis_static
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${hiredis_dir}/include>")
  set_target_properties(
    libhiredis_ssl_static
    PROPERTIES
    INTERFACE_LINK_LIBRARIES OpenSSL::SSL
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${hiredis_dir}/include>")

  set(hiredis_FOUND TRUE)
  set(hiredis_ssl_FOUND TRUE)

  set(target libhiredis_static)
endif()

if(HIREDIS_SSL_INCLUDE_DIR AND HIREDIS_SSL_LIBRARY)
  mark_as_advanced(HIREDIS_SSL_INCLUDE_DIR HIREDIS_SSL_LIBRARY)

  add_library(HIREDIS::HIREDIS_SSL UNKNOWN IMPORTED)
  set_target_properties(
    HIREDIS::HIREDIS_SSL
    PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_SSL_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${HIREDIS_SSL_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_SSL_INCLUDE_DIR}")

  set(hiredis_ssl_FOUND TRUE)
elseif(HIREDIS_FROM_INTERNET)
elseif(REDISS_STORAGE_BACKEND)
  message(STATUS
    "please install libhiredis_ssl or use -DHIREDIS_FROM_INTERNET=ON or disable with -DREDISS_STORAGE_BACKEND=OFF"
  )
endif()

if(WIN32 AND hiredis_FOUND)
  target_link_libraries(${target} INTERFACE ws2_32)
endif()

include(FeatureSummary)
set_package_properties(
  hiredis
  PROPERTIES
  URL "https://github.com/redis/hiredis"
  DESCRIPTION "Hiredis is a minimalistic C client library for the Redis database"
)
