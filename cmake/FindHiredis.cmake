mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_HIREDIS STREQUAL "DOWNLOAD")
  message(STATUS "Downloading Hiredis as requested")
  set(_download_hiredis TRUE)
else()
  find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
  find_library(HIREDIS_LIBRARY hiredis)
  if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
    file(READ "${HIREDIS_INCLUDE_DIR}/hiredis/hiredis.h" _hiredis_h)
    string(REGEX MATCH "#define HIREDIS_MAJOR +([0-9]+).*#define HIREDIS_MINOR +([0-9]+).*#define HIREDIS_PATCH +([0-9]+)" _ "${_hiredis_h}")
    set(_hiredis_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_hiredis_version_string}" VERSION_GREATER_EQUAL "${Hiredis_FIND_VERSION}")
      message(STATUS "Using system Hiredis (${HIREDIS_LIBRARY})")
      set(_hiredis_origin "SYSTEM (${HIREDIS_LIBRARY})")
      add_library(dep_hiredis UNKNOWN IMPORTED)
      set_target_properties(
        dep_hiredis
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
  set(_hiredis_version_string 1.3.0)

  include(FetchContent)
  FetchContent_Declare(
    Hiredis
    URL "https://github.com/redis/hiredis/archive/refs/tags/v${_hiredis_version_string}.tar.gz"
    URL_HASH SHA256=25cee4500f359cf5cad3b51ed62059aadfc0939b05150c1f19c7e2829123631c
    # Intentionally not using hiredis's build system since it doesn't put
    # headers in a hiredis subdirectory. Reference:
    # https://discourse.cmake.org/t/prevent-fetchcontent-makeavailable-to-execute-cmakelists-txt/12704/3
    SOURCE_SUBDIR dummy
  )
  FetchContent_MakeAvailable(Hiredis)

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
