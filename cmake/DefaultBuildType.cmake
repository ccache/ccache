# Set a default build type if none was specified

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  # Default to Release for zip builds and Debug for git builds
  if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
  else()
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build."
                                         FORCE)
  endif()
  message(
    STATUS
      "Setting CMAKE_BUILD_TYPE to '${CMAKE_BUILD_TYPE}' as none was specified."
  )

  # Set the possible values of build type for ccmake
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                               "MinSizeRel" "RelWithDebInfo")
endif()
