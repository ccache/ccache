# Note: This is part of CMakeLists.txt file, not to be confused with
# CPackConfig.cmake.

find_program(NINJA_EXE NAMES "ninja" DOC "Path to Ninja executable")
mark_as_advanced(NINJA_EXE) # Don't show in CMake UIs
if(NINJA_EXE)
  set(CPACK_CMAKE_GENERATOR "Ninja")
else()
  set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
endif()

# Make it obvious which version is used.
set(CMAKE_DEBUG_POSTFIX "-d")

if(${CMAKE_VERSION} VERSION_LESS "3.9")
  set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_DESCRIPTION}")
endif()

# From GenerateVersionFile.cmake.
set(CPACK_PACKAGE_VERSION ${VERSION})

set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ")

list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/\\.git")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/build[-_/]")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_BINARY_DIR}")

set(CPACK_PACKAGE_FILE_NAME "ccache-binary")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "ccache-src")

include(CPack)
