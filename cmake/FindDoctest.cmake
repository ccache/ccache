mark_as_advanced(DOCTEST_INCLUDE_DIR)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_DOCTEST STREQUAL "DOWNLOAD")
  message(STATUS "Downloading Doctest as requested")
  set(_download_doctest TRUE)
else()
  find_path(DOCTEST_INCLUDE_DIR doctest/doctest.h)
  if(DOCTEST_INCLUDE_DIR)
    file(READ "${DOCTEST_INCLUDE_DIR}/doctest/doctest.h" _doctest_h)
    string(REGEX MATCH "#define DOCTEST_VERSION_MAJOR ([0-9]+).*#define DOCTEST_VERSION_MINOR ([0-9]+).*#define DOCTEST_VERSION_PATCH ([0-9]+)" _ "${_doctest_h}")
    set(_doctest_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_doctest_version_string}" VERSION_GREATER_EQUAL "${Doctest_FIND_VERSION}")
      message(STATUS "Using system doctest (${DOCTEST_INCLUDE_DIR}/doctest/doctest.h)")
      set(_doctest_origin "SYSTEM (${DOCTEST_INCLUDE_DIR}/doctest/doctest.h)")
      add_library(dep_doctest INTERFACE IMPORTED)
      target_include_directories(dep_doctest INTERFACE "${DOCTEST_INCLUDE_DIR}")
    endif()
  endif()
  if(NOT _doctest_origin)
    if(DEPS STREQUAL "AUTO")
      message(STATUS "Downloading doctest from the internet since Doctest>=${Doctest_FIND_VERSION} was not found locally and DEPS=AUTO")
      set(_download_doctest TRUE)
    else()
      message(FATAL_ERROR "Could not find Doctest>=${Doctest_FIND_VERSION}")
    endif()
  endif()
endif()

if(_download_doctest)
  set(_doctest_origin DOWNLOADED)
  set(_doctest_version_string 2.4.11)

  add_header_only_library(
    doctest
    URL "https://github.com/doctest/doctest/releases/download/v${_doctest_version_string}/doctest.h"
    SHA256 44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb
    SUBDIR doctest
  )
endif()

register_dependency(Doctest "${_doctest_origin}" "${_doctest_version_string}")
