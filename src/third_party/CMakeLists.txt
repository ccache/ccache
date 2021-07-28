add_library(third_party STATIC base32hex.c format.cpp url.cpp xxhash.c)
if(NOT MSVC)
  target_sources(third_party PRIVATE getopt_long.c)
else()
  target_sources(third_party PRIVATE win32/getopt.c)
  target_compile_definitions(third_party PUBLIC -DSTATIC_GETOPT)
endif()

if(WIN32)
  target_sources(third_party PRIVATE win32/mktemp.c)
endif ()

if(ENABLE_TRACING)
  target_sources(third_party PRIVATE minitrace.c)
endif()

set(xxhdispatchtest [=[
#include "xxh_x86dispatch.c"

int main()
{
  XXH3_64bits_dispatch("foo", 3);
  return 1;
}
]=])

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xxhdispatchtest.c" "${xxhdispatchtest}")

try_compile(USE_XXH_DISPATCH ${CMAKE_CURRENT_BINARY_DIR}
  "${CMAKE_CURRENT_BINARY_DIR}/xxhdispatchtest.c"
  CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}"
  COMPILE_DEFINITIONS "-DXXH_STATIC_LINKING_ONLY")

target_compile_definitions(third_party INTERFACE "-DXXH_STATIC_LINKING_ONLY")
if(USE_XXH_DISPATCH)
  target_sources(third_party PRIVATE xxh_x86dispatch.c)
  target_compile_definitions(third_party INTERFACE "-DUSE_XXH_DISPATCH")
endif()

# Treat third party headers as system files (no warning for those headers).
target_include_directories(
  third_party
  PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} SYSTEM
)

target_link_libraries(third_party PRIVATE standard_settings)
target_link_libraries(third_party INTERFACE blake3)

# Silence warning from winbase.h due to /Zc:preprocessor.
if(MSVC)
  target_compile_options(third_party PRIVATE /wd5105)
endif()

add_subdirectory(blake3)
