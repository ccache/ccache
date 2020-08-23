# Set a default build type if none was specified.

if(CMAKE_BUILD_TYPE OR CMAKE_CONFIGURATION_TYPES)
  return()
endif()

# Default to Release for end user builds (from source archive) and Debug for
# development builds (in a Git repository).
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
  set(
    CMAKE_BUILD_TYPE "Debug"
    CACHE STRING "Choose the type of build." FORCE)
else()
  set(
    CMAKE_BUILD_TYPE "Release"
    CACHE STRING "Choose the type of build." FORCE)
endif()
message(
  STATUS
  "Setting CMAKE_BUILD_TYPE to ${CMAKE_BUILD_TYPE} as none was specified."
)

# Set the possible values of build type for CMake UIs.
set_property(
  CACHE CMAKE_BUILD_TYPE
  PROPERTY
  STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
