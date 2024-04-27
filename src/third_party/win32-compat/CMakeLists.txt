add_library(win32_compat STATIC EXCLUDE_FROM_ALL win32/mktemp.c)

if(MSVC)
  target_sources(win32_compat PRIVATE win32/getopt.c)
  target_compile_definitions(win32_compat PUBLIC STATIC_GETOPT)
endif()

# Silence warning from winbase.h due to /Zc:preprocessor.
if(MSVC)
  target_compile_options(win32_compat PRIVATE /wd5105)
endif()

target_include_directories(win32_compat INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
