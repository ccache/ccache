mark_as_advanced(REDISPLUSPLUS_INCLUDE_DIR REDISPLUSPLUS_LIBRARY)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_REDISPLUSPLUS STREQUAL "DOWNLOAD")
  message(STATUS "Downloading redis-plus-plus as requested")
  set(_download_redisplusplus TRUE)
else()
  find_path(REDISPLUSPLUS_INCLUDE_DIR sw)
  find_library(REDISPLUSPLUS_LIBRARY redis++)
  if(REDISPLUSPLUS_INCLUDE_DIR AND REDISPLUSPLUS_LIBRARY)
    file(READ "${REDISPLUSPLUS_INCLUDE_DIR}/redis++/version.h" _redisplusplus_h)
    string(REGEX MATCH "#define VERSION_MAJOR +([0-9]+).*#define VERSION_MINOR +([0-9]+).*#define VERSION_PATCH +([0-9]+)" _ "${_redisplusplus_h}")
    set(_redisplusplus_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_redisplusplus_version_string}" VERSION_GREATER_EQUAL "${RedisPlusPlus_FIND_VERSION}")
      message(STATUS "Using system RedisPlusPlus (${REDISPLUSPLUS_LIBRARY})")
      set(_redisplusplus_origin "SYSTEM (${REDISPLUSPLUS_LIBRARY})")
      add_library(dep_redisplusplus UNKNOWN IMPORTED)
      set_target_properties(
        dep_redisplusplus
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
        IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
      )
    endif()
  endif()
  if(NOT _hiredis_origin)
    if(DEPS STREQUAL "AUTO")
      message(STATUS "Downloading Hiredis from the internet since Hiredis>=${Hiredis_FIND_VERSION} was not found locally and DEPS=AUTO")
      set(_download_hiredis TRUE)
    else()
      message(FATAL_ERROR "Could not find Hiredis>=${Hiredis_FIND_VERSION}")
    endif()
  endif()
endif()

if(_download_hiredis)
  set(_hiredis_origin DOWNLOADED)
  set(_hiredis_version_string 1.2.0)

  include(FetchContent)
  FetchContent_Declare(
    Hiredis
    URL "https://github.com/redis/hiredis/archive/refs/tags/v${_hiredis_version_string}.tar.gz"
    URL_HASH SHA256=82ad632d31ee05da13b537c124f819eb88e18851d9cb0c30ae0552084811588c
  )

  # Intentionally not using hiredis's build system since it doesn't put headers
  # in a hiredis subdirectory.
  FetchContent_Populate(Hiredis)
  set(
    _hiredis_sources
    "${hiredis_SOURCE_DIR}/alloc.c"
    "${hiredis_SOURCE_DIR}/async.c"
    "${hiredis_SOURCE_DIR}/dict.c"
    "${hiredis_SOURCE_DIR}/hiredis.c"
    "${hiredis_SOURCE_DIR}/net.c"
    "${hiredis_SOURCE_DIR}/read.c"
    "${hiredis_SOURCE_DIR}/sds.c"
    "${hiredis_SOURCE_DIR}/sockcompat.c"
  )
  add_library(dep_hiredis STATIC EXCLUDE_FROM_ALL "${_hiredis_sources}")
  if(WIN32)
    target_compile_definitions(dep_hiredis PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  make_directory("${hiredis_SOURCE_DIR}/include/hiredis")
  file(GLOB _hiredis_headers "${hiredis_SOURCE_DIR}/*.h")
  file(COPY ${_hiredis_headers} DESTINATION "${hiredis_SOURCE_DIR}/include/hiredis")
  target_include_directories(
    dep_hiredis SYSTEM INTERFACE "$<BUILD_INTERFACE:${hiredis_SOURCE_DIR}/include>"
  )
endif()

if(WIN32)
  target_link_libraries(dep_hiredis INTERFACE ws2_32)
endif()

register_dependency(Hiredis "${_hiredis_origin}" "${_hiredis_version_string}")
