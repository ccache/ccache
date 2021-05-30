add_library(third_party_lib STATIC base32hex.c format.cpp xxhash.c)
if(NOT MSVC)
  target_sources(third_party_lib PRIVATE getopt_long.c)
else()
  target_sources(third_party_lib PRIVATE win32/getopt.c)
  target_compile_definitions(third_party_lib PUBLIC -DSTATIC_GETOPT)
endif()

if(WIN32)
  target_sources(third_party_lib PRIVATE win32/mktemp.c)
endif ()

if(ENABLE_TRACING)
  target_sources(third_party_lib PRIVATE minitrace.c)
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

target_compile_definitions(third_party_lib INTERFACE "-DXXH_STATIC_LINKING_ONLY")
if(USE_XXH_DISPATCH)
  target_sources(third_party_lib PRIVATE xxh_x86dispatch.c)
  target_compile_definitions(third_party_lib INTERFACE "-DUSE_XXH_DISPATCH")
endif()

# Treat third party headers as system files (no warning for those headers).
target_include_directories(
  third_party_lib
  PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} SYSTEM)

target_link_libraries(third_party_lib PRIVATE standard_settings)
target_link_libraries(third_party_lib INTERFACE blake3)

# Silence warning from winbase.h due to /Zc:preprocessor.
if(MSVC)
  target_compile_options(
    third_party_lib
    PRIVATE /wd5105)
endif()

add_subdirectory(blake3)
