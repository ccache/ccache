mark_as_advanced(XXHASH_INCLUDE_DIR XXHASH_LIBRARY)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_XXHASH STREQUAL "DOWNLOAD")
  message(STATUS "Downloading Xxhash as requested")
  set(_download_xxhash TRUE)
else()
  find_path(XXHASH_INCLUDE_DIR xxhash.h)
  find_library(XXHASH_LIBRARY xxhash)
  if(XXHASH_INCLUDE_DIR AND XXHASH_LIBRARY)
    file(READ "${XXHASH_INCLUDE_DIR}/xxhash.h" _xxhash_h)
    string(REGEX MATCH "#define XXH_VERSION_MAJOR +([0-9]+).*#define XXH_VERSION_MINOR +([0-9]+).*#define XXH_VERSION_RELEASE +([0-9]+)" _ "${_xxhash_h}")
    set(_xxhash_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_xxhash_version_string}" VERSION_GREATER_EQUAL "${Xxhash_FIND_VERSION}")
      message(STATUS "Using system Xxhash (${XXHASH_LIBRARY})")
      set(_xxhash_origin "SYSTEM (${XXHASH_LIBRARY})")
      add_library(dep_xxhash UNKNOWN IMPORTED)
      set_target_properties(
        dep_xxhash
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${XXHASH_INCLUDE_DIR}"
        IMPORTED_LOCATION "${XXHASH_LIBRARY}"
      )
    endif()
  endif()
  if(NOT TARGET dep_xxhash)
    if(DEPS STREQUAL "AUTO")
      message(STATUS "Downloading Xxhash from the internet since Xxhash>=${Xxhash_FIND_VERSION} was not found locally and DEPS=AUTO")
      set(_download_xxhash TRUE)
    else()
      message(FATAL_ERROR "Could not find Xxhash>=${Xxhash_FIND_VERSION}")
    endif()
  endif()
endif()

if(_download_xxhash)
  set(_xxhash_origin DOWNLOADED)
  set(_xxhash_version_string 0.8.3)

  include(FetchContent)
  FetchContent_Declare(
    Xxhash
    URL "https://github.com/Cyan4973/xxhash/archive/refs/tags/v${_xxhash_version_string}.tar.gz"
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
  )

  FetchContent_Populate(Xxhash)
  set(_xxhash_sources "${xxhash_SOURCE_DIR}/xxhash.c")
  if(PLATFORM STREQUAL x86_64 OR PLATFORM STREQUAL AMD64)
    list(APPEND _xxhash_sources "${xxhash_SOURCE_DIR}/xxh_x86dispatch.c")
  endif()
  add_library(dep_xxhash STATIC EXCLUDE_FROM_ALL "${_xxhash_sources}")
  target_compile_definitions(dep_xxhash INTERFACE XXH_STATIC_LINKING_ONLY)
  if(PLATFORM STREQUAL x86_64 OR PLATFORM STREQUAL AMD64)
    target_compile_definitions(dep_xxhash INTERFACE USE_XXH_DISPATCH)
  endif()
  if(WIN32)
    target_compile_definitions(dep_xxhash PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  target_include_directories(
    dep_xxhash SYSTEM INTERFACE "$<BUILD_INTERFACE:${xxhash_SOURCE_DIR}>"
  )
endif()

if(WIN32)
  target_link_libraries(dep_xxhash INTERFACE ws2_32)
endif()

register_dependency(Xxhash "${_xxhash_origin}" "${_xxhash_version_string}")
