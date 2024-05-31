mark_as_advanced(BLAKE3_INCLUDE_DIR)
mark_as_advanced(BLAKE3_LIBRARY)

if(DEP_BLAKE3 STREQUAL "BUNDLED")
  message(STATUS "Using bundled Blake3 as requested")
else()
  find_path(BLAKE3_INCLUDE_DIR blake3.h)
  find_library(BLAKE3_LIBRARY blake3)
  if(BLAKE3_INCLUDE_DIR)
    file(READ "${BLAKE3_INCLUDE_DIR}/blake3.h" _blake3_h)
    string(REGEX MATCH "#define BLAKE3_VERSION_STRING \"([0-9]+).([0-9]+).*([0-9]+)\"" _ "${_blake3_h}")
    set(_blake3_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_blake3_version_string}" VERSION_GREATER_EQUAL "${Blake3_FIND_VERSION}")
      if(BLAKE3_LIBRARY)
        message(STATUS "Using system Blake3 (${BLAKE3_LIBRARY})")
        add_library(dep_blake3 UNKNOWN IMPORTED)
        set_target_properties(
          dep_blake3
          PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${BLAKE3_INCLUDE_DIR}"
          IMPORTED_LOCATION "${BLAKE3_LIBRARY}"
        )
      endif()
      register_dependency(Blake3 "SYSTEM (${BLAKE3_LIBRARY})" "${_blake3_version_string}")
    endif()
  endif()
  if(NOT TARGET dep_blake3)
    message(STATUS "Using bundled Blake3 since Blake3>=${Blake3_FIND_VERSION} was not found locally")
  endif()
endif()
