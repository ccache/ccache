configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/version.cpp.in
  ${CMAKE_BINARY_DIR}/src/ccache/version.cpp
  @ONLY)

if(WIN32)

  if(CCACHE_VERSION MATCHES "^([0-9]+)([0-9]+)([0-9]+)$")
    set(CCACHE_RCVERSION_MAJOR "${CMAKE_MATCH_1}")
    set(CCACHE_RCVERSION_MINOR "${CMAKE_MATCH_2}")
    set(CCACHE_RCVERSION_PATCH "${CMAKE_MATCH_3}")
    set(CCACHE_RCVERSION ${CCACHE_RCVERSION_MAJOR},${CCACHE_RCVERSION_MINOR},${CCACHE_RCVERSION_PATCH},0)
  else()
    set(CCACHE_RCVERSION 0,0,0,0)
  endif()


  configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_BINARY_DIR}/version.rc
    @ONLY)

  configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/ccache.exe.manifest.in
    ${CMAKE_BINARY_DIR}/ccache.exe.manifest
    @ONLY)
endif()
