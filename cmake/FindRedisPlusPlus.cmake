mark_as_advanced(REDISPLUSPLUS_INCLUDE_DIR REDISPLUSPLUS_LIBRARY)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_REDISPLUSPLUS STREQUAL "DOWNLOAD")
  message(STATUS "Downloading Redisplusplus as requested")
  set(_download_redisplusplus TRUE)
else()
  find_path(REDISPLUSPLUS_INCLUDE_DIR sw)
  find_library(REDISPLUSPLUS_LIBRARY redis++)
  if(REDISPLUSPLUS_INCLUDE_DIR AND REDISPLUSPLUS_LIBRARY)
    file(READ "${REDISPLUSPLUS_INCLUDE_DIR}/sw/redis++/version.h" _redisplusplus_h)
    string(REGEX MATCH ".*const int VERSION_MAJOR = ([0-9]+).*const int VERSION_MINOR = ([0-9]+).*const int VERSION_PATCH = ([0-9]+)" _ "${_redisplusplus_h}")
    set(_redisplusplus_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_redisplusplus_version_string}" VERSION_GREATER_EQUAL "${RedisPlusPlus_FIND_VERSION}")
      message(STATUS "Using system RedisPlusPlus (${REDISPLUSPLUS_LIBRARY})")
      set(_redisplusplus_origin "SYSTEM (${REDISPLUSPLUS_LIBRARY})")
      add_library(dep_redisplusplus UNKNOWN IMPORTED)
      set_target_properties(
        dep_redisplusplus
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${REDISPLUSPLUS_INCLUDE_DIR}"
        IMPORTED_LOCATION "${REDISPLUSPLUS_LIBRARY}"
      )
    endif()
  endif()
  if(NOT _redisplusplus_origin)
    if(DEPS STREQUAL "AUTO")
      message(STATUS "Downloading RedisPlusPlus from the internet since RedisPlusPlus>=${RedisPlusPlus_FIND_VERSION} was not found locally and DEPS=AUTO")
      set(_download_redisplusplus TRUE)
    else()
      message(FATAL_ERROR "Could not find RedisPlusPlus>=${RedisPlusPlus_FIND_VERSION}")
    endif()
  endif()
endif()

if(_download_redisplusplus)
  message(FATAL_ERROR "Download not supported yet: RedisPlusPlus>=${RedisPlusPlus_FIND_VERSION}")
endif()

if(WIN32)
  target_link_libraries(dep_redisplusplus INTERFACE ws2_32)
endif()

register_dependency(RedisPlusPlus "${_redisplusplus_origin}" "${_redisplusplus_version_string}")
