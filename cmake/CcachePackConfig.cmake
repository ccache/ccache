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

set(CPACK_PACKAGE_FILE_NAME "ccache-binary")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "ccache-src")

include(CPack)
