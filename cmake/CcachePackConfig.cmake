# Note: This is part of CMakeLists.txt file, not to be confused with
# CPackConfig.cmake.

# From CcacheVersion.cmake.
set(CPACK_PACKAGE_VERSION ${CCACHE_VERSION})

set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TXZ")
endif()

set(
  CPACK_PACKAGE_FILE_NAME
  "ccache-${CCACHE_VERSION}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}"
)

include(CPack)
