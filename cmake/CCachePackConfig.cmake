# Note: this is part of CMakeLists.txt file, not to be confused with
# CPackConfig.cmake

find_program(NINJA_EXE NAMES "ninja" DOC "Path to ninja executable")
mark_as_advanced(NINJA_EXE) # don't show in ccmake
if(NINJA_EXE)
  set(CPACK_CMAKE_GENERATOR "Ninja")
else()
  set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
endif()

# make obvious which version is used
set(CMAKE_DEBUG_POSTFIX "-d")

if(${CMAKE_VERSION} VERSION_LESS "3.9")
  set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_DESCRIPTION}")
endif()

# from GenerateVersionFile.cmake
set(CPACK_PACKAGE_VERSION ${VERSION})

set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ")

# Default includes build directory, so improve it: Include buildenv, but exclude
# other build directories like /build/, /build-* and /build_*
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/\\.git")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_SOURCE_DIR}/build[\\-_/]")
list(APPEND CPACK_SOURCE_IGNORE_FILES "^${CMAKE_BINARY_DIR}")

# A top level directory is nice for extracting in-place, but prevents extracting
# directly to e.g. /usr/local. Unfortunately this is not split between binary
# and source build, so there is always no top level directory.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

set(CPACK_PACKAGE_FILE_NAME "ccache-binary")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "ccache-src")

include(CPack)
