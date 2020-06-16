if(libb2_FOUND)
  return()
endif()

if(USE_LIBB2_FROM_INTERNET)
  set(libb2_version ${libb2_FIND_VERSION})
  set(libb2_url
      https://github.com/BLAKE2/libb2/releases/download/v${libb2_version}/libb2-${libb2_version}.tar.gz
  )

  set(libb2_dir ${CMAKE_BINARY_DIR}/libb2-${libb2_version})
  set(libb2_build ${CMAKE_BINARY_DIR}/libb2-build)

  if(NOT EXISTS "${libb2_dir}.tar.gz")
    file(DOWNLOAD "${libb2_url}" "${libb2_dir}.tar.gz")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${libb2_dir}.tar.gz"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

  file(
    WRITE "${libb2_dir}/src/config.h.cmake.in"
    [=[
/* Define if you have the `explicit_bzero' function. */
#cmakedefine HAVE_EXPLICIT_BZERO
/* Define if you have the `explicit_memset' function. */
#cmakedefine HAVE_EXPLICIT_MEMSET
/* Define if you have the `memset' function. */
#cmakedefine HAVE_MEMSET
/* Define if you have the `memset_s' function. */
#cmakedefine HAVE_MEMSET_S
]=])

  file(READ "cmake/Libb2CMakeLists.txt" libb2_cmakelists)
  file(WRITE "${libb2_dir}/src/CMakeLists.txt" "${libb2_cmakelists}")
  add_subdirectory("${libb2_dir}/src" "${libb2_build}" EXCLUDE_FROM_ALL)

  add_library(libb2::libb2 ALIAS libb2)
  set_target_properties(
    libb2
    PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${libb2_dir}/src")

  set(libb2_FOUND TRUE)
else()
  find_library(LIBB2_LIBRARY NAMES b2 libb2)
  find_path(LIBB2_INCLUDE_DIR blake2.h)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    libb2 "please install libb2 or use -DUSE_LIBB2_FROM_INTERNET=ON"
    LIBB2_INCLUDE_DIR LIBB2_LIBRARY)
  mark_as_advanced(LIBB2_INCLUDE_DIR LIBB2_LIBRARY)

  add_library(libb2::libb2 UNKNOWN IMPORTED)
  set_target_properties(
    libb2::libb2
    PROPERTIES
    IMPORTED_LOCATION "${LIBB2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBB2_INCLUDE_DIR}")
endif()

include(FeatureSummary)
set_package_properties(
  libb2
  PROPERTIES
  URL "http://blake2.net/"
  DESCRIPTION "C library providing BLAKE2b, BLAKE2s, BLAKE2bp, BLAKE2sp")
