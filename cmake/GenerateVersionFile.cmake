set(version_file ${CMAKE_SOURCE_DIR}/VERSION)

if(EXISTS ${version_file})
  file(READ ${version_file} VERSION)
  string(STRIP ${VERSION} VERSION)
else()
  include(CcacheVersion)
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/version.cpp.in
  ${CMAKE_BINARY_DIR}/src/version.cpp
  @ONLY)

message(STATUS "Ccache version: ${VERSION}")
