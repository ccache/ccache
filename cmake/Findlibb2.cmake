if (libb2_FOUND)
  return()
endif()

if (USE_LIBB2_FROM_INTERNET)
  set(libb2_version ${libb2_FIND_VERSION})
  set(libb2_url https://github.com/BLAKE2/libb2/releases/download/v${libb2_version}/libb2-${libb2_version}.tar.gz)

  set(libb2_dir ${CMAKE_BINARY_DIR}/libb2-${libb2_version})
  set(libb2_build ${CMAKE_BINARY_DIR}/libb2-build)

  file(DOWNLOAD "${libb2_url}" "${CMAKE_BINARY_DIR}/libb2.tar.gz")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/libb2.tar.gz"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

  file(WRITE "${libb2_dir}/src/config.h.cmake.in" [=[
    /* Define if you have the `explicit_bzero' function. */
    #cmakedefine HAVE_EXPLICIT_BZERO
    /* Define if you have the `explicit_memset' function. */
    #cmakedefine HAVE_EXPLICIT_MEMSET
    /* Define if you have the `memset' function. */
    #cmakedefine HAVE_MEMSET
    /* Define if you have the `memset_s' function. */
    #cmakedefine HAVE_MEMSET_S
    ]=])

  file(WRITE "${libb2_dir}/src/CMakeLists.txt" [=[
    project(libb2 C)

    include(CheckFunctionExists)
    foreach(func IN ITEMS
    explicit_bzero
    explicit_memset
    memset
    memset_s
    )
    string(TOUPPER ${func} func_var)
    set(func_var HAVE_${func_var})
    check_function_exists(${func} ${func_var})
    endforeach()

    configure_file(config.h.cmake.in config.h)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)

    add_library(libblake2b_ref STATIC blake2b-ref.c blake2s-ref.c)
    target_compile_definitions(libblake2b_ref PRIVATE SUFFIX=_ref)

    function(add_libblake2b name suffix)
    add_library(${name} STATIC blake2b.c blake2s.c)
    target_compile_definitions(${name} PRIVATE ${suffix})
    target_compile_options(${name} PRIVATE ${ARGN})
    endfunction()

    add_libblake2b(libblake2b_sse2 SUFFIX=_sse2 -msse2)
    add_libblake2b(libblake2b_ssse3 SUFFIX=_ssse3 -msse2 -mssse3)
    add_libblake2b(libblake2b_sse41 SUFFIX=_sse41 -msse2 -mssse3 -msse4.1)
    add_libblake2b(libblake2s_avx SUFFIX=_avx -msse2 -mssse3 -msse4.1 -mavx)
    add_libblake2b(libblake2b_xop SUFFIX=_xop -msse2 -mssse3 -msse4.1 -mavx -mxop)

    add_library(libb2 STATIC blake2-dispatch.c)
    target_link_libraries(libb2
    PUBLIC
      libblake2b_ref libblake2b_sse2 libblake2b_ssse3
      libblake2b_sse41 libblake2s_avx libblake2b_xop
    )
  ]=])
  add_subdirectory("${libb2_dir}/src" "${libb2_build}" EXCLUDE_FROM_ALL)

  add_library(libb2::libb2 ALIAS libb2)
  set_target_properties(libb2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${libb2_dir}/src"
  )

  set(libb2_FOUND TRUE)
else()
  find_library(LIBB2_LIBRARY b2)
  find_path(LIBB2_INCLUDE_DIR blake2b.h)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(libb2
    DEFAULT_MSG
    LIBB2_INCLUDE_DIR LIBB2_LIBRARY
  )
  mark_as_advanced(LIBB2_INCLUDE_DIR LIBB2_LIBRARY)

  add_library(libb2::libb2 UNKNOWN IMPORTED)
  set_target_properties(libb2::libb2 PROPERTIES
    IMPORTED_LOCATION "${LIBB2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBB2_INCLUDE_DIR}"
  )
endif()


include(FeatureSummary)
set_package_properties(libb2 PROPERTIES
  URL "http://blake2.net/"
  DESCRIPTION "C library providing BLAKE2b, BLAKE2s, BLAKE2bp, BLAKE2sp")
