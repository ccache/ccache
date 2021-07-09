if(hiredis_FOUND)
  return()
endif()

if(HIREDIS_FROM_INTERNET)
  # Although ${hiredis_FIND_VERSION} was requested, let's download a newer version.
  set(hiredis_version "1.0.0")
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
  make_directory("${hiredis_dir}/hiredis")
  file(GLOB HIREDIS_HEADERS "${hiredis_dir}/*.h")
  file(COPY ${HIREDIS_HEADERS} DESTINATION "${hiredis_dir}/hiredis")

  add_subdirectory("${hiredis_dir}" "${hiredis_build}" EXCLUDE_FROM_ALL)

  add_library(HIREDIS::HIREDIS ALIAS hiredis)
  set_target_properties(
    hiredis
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${hiredis_dir}>")

  set(hiredis_FOUND TRUE)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(HIREDIS REQUIRED hiredis>=${hiredis_FIND_VERSION})
  find_library(HIREDIS_LIBRARY ${HIREDIS_LIBRARIES} HINTS ${HIREDIS_LIBDIR})
  find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ${HIREDIS_PREFIX}/include)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    hiredis "please install libhiredis or use -DHIREDIS_FROM_INTERNET=ON"
    HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)
  mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

  add_library(HIREDIS::HIREDIS UNKNOWN IMPORTED)
  set_target_properties(
    HIREDIS::HIREDIS
    PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${HIREDIS_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}")
endif()

include(FeatureSummary)
set_package_properties(
  hiredis
  PROPERTIES
  URL "https://github.com/redis/hiredis"
  DESCRIPTION "Hiredis is a minimalistic C client library for the Redis database")
