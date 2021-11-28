if(hiredis_FOUND)
  return()
endif()

if(HIREDIS_FROM_INTERNET)
  set(hiredis_version "1.0.2")
  set(hiredis_url https://github.com/redis/hiredis/archive/v${hiredis_version}.tar.gz)

  set(hiredis_dir ${CMAKE_BINARY_DIR}/hiredis-${hiredis_version})
  set(hiredis_build ${CMAKE_BINARY_DIR}/hiredis-build)

  if(NOT EXISTS "${hiredis_dir}.tar.gz")
    file(DOWNLOAD "${hiredis_url}" "${hiredis_dir}.tar.gz" STATUS download_status)
    list(GET download_status 0 error_code)
    if(error_code)
      file(REMOVE "${hiredis_dir}.tar.gz")
      list(GET download_status 1 error_message)
      message(FATAL "Failed to download hiredis: ${error_message}")
    endif()
  endif()

  execute_process(
    COMMAND tar xf "${hiredis_dir}.tar.gz"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    RESULT_VARIABLE tar_error)
  if(NOT tar_error EQUAL 0)
    message(FATAL "extracting ${hiredis_dir}.tar.gz failed")
  endif()

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
  add_library(libhiredis_static STATIC EXCLUDE_FROM_ALL ${hiredis_sources})
  add_library(HIREDIS::HIREDIS ALIAS libhiredis_static)
  if(WIN32)
    target_compile_definitions(libhiredis_static PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(libhiredis_static PUBLIC ws2_32)
  endif()

  make_directory("${hiredis_dir}/include")
  make_directory("${hiredis_dir}/include/hiredis")
  file(GLOB hiredis_headers "${hiredis_dir}/*.h")
  file(COPY ${hiredis_headers} DESTINATION "${hiredis_dir}/include/hiredis")
  set_target_properties(
    libhiredis_static
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${hiredis_dir}/include>")
else()
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(HIREDIS hiredis>=${hiredis_FIND_VERSION})
    find_library(HIREDIS_LIBRARY ${HIREDIS_LIBRARIES} HINTS ${HIREDIS_LIBDIR})
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ${HIREDIS_PREFIX}/include)
  else()
    find_library(HIREDIS_LIBRARY hiredis)
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
  endif()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    hiredis
    "please install libhiredis or use -DHIREDIS_FROM_INTERNET=ON or disable with -DREDIS_STORAGE_BACKEND=OFF"
    HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)
  mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

  if(PKG_CONFIG_FOUND)
    pkg_check_modules(HIREDIS_SSL hiredis_ssl>=${hiredis_FIND_VERSION})
    find_library(HIREDIS_SSL_LIBRARY ${HIREDIS_SSL_LIBRARIES} HINTS ${HIREDIS_SSL_LIBDIR})
    find_path(HIREDIS_SSL_INCLUDE_DIR hiredis/hiredis_ssl.h HINTS ${HIREDIS_SSL_PREFIX}/include)
  else()
    find_library(HIREDIS_SSL_LIBRARY hiredis_ssl)
    find_path(HIREDIS_SSL_INCLUDE_DIR hiredis/hiredis_ssl.h)
  endif()

  mark_as_advanced(HIREDIS_SSL_INCLUDE_DIR HIREDIS_SSL_LIBRARY)
  set(hiredis_ssl_FOUND HIREDIS_SSL_LIBRARY)

  add_library(HIREDIS::HIREDIS UNKNOWN IMPORTED)
  set_target_properties(
    HIREDIS::HIREDIS
    PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${HIREDIS_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}")

  add_library(HIREDIS::HIREDIS_SSL UNKNOWN IMPORTED)
  set_target_properties(
    HIREDIS::HIREDIS_SSL
    PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_SSL_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${HIREDIS_SSL_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_SSL_INCLUDE_DIR}")
endif()

include(FeatureSummary)
set_package_properties(
  hiredis
  PROPERTIES
  URL "https://github.com/redis/hiredis"
  DESCRIPTION "Hiredis is a minimalistic C client library for the Redis database")

set(hiredis_FOUND TRUE)
