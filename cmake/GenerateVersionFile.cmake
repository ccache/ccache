include(CcacheVersion)
configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/version.cpp.in
  ${CMAKE_BINARY_DIR}/src/version.cpp
  @ONLY)
message(STATUS "Ccache version: ${VERSION}")
