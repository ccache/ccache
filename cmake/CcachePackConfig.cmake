# Note: This is part of CMakeLists.txt file, not to be confused with
# CPackConfig.cmake.

if(${CMAKE_VERSION} VERSION_LESS "3.9")
  set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_DESCRIPTION}")
endif()

# From GenerateVersionFile.cmake.
set(CPACK_PACKAGE_VERSION ${VERSION})

set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TXZ")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ;TXZ")

list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/\\.git")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/build")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_BINARY_DIR}")

set(
  CPACK_PACKAGE_FILE_NAME
  "ccache-${VERSION}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}"
)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "ccache-${VERSION}")

configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/PreparePackage.cmake.in
  ${CMAKE_BINARY_DIR}/PreparePackage.cmake
  @ONLY
)

if(${CMAKE_VERSION} VERSION_LESS "3.16")
  set(CPACK_INSTALL_SCRIPT ${CMAKE_BINARY_DIR}/PreparePackage.cmake)
else()
  set(CPACK_INSTALL_SCRIPTS ${CMAKE_BINARY_DIR}/PreparePackage.cmake)
endif()

include(CPack)
