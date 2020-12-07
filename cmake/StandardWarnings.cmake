# This file provides a special "standard_warnings" target which is supposed to
# be linked privately by all product and test code, but not by third party code.
add_library(standard_warnings INTERFACE)

if(CCACHE_DEV_MODE)
  # Enabled by default for developer builds.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" TRUE)
else()
  # Disabled by default for user builds so compilation doesn't fail with new
  # compilers that may emit new warnings.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" FALSE)
endif()

if(NOT MSVC)
  set(CCACHE_COMPILER_WARNINGS -Wall)
endif()

if(CCACHE_DEV_MODE)
  include(DevModeWarnings)
endif()

target_compile_options(standard_warnings INTERFACE ${CCACHE_COMPILER_WARNINGS})
