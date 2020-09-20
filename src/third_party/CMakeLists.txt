set(third_party_source_files format.cpp xxhash.c)

if(NOT MSVC)
  list(APPEND third_party_source_files getopt_long.c)
else()
  list(APPEND third_party_source_files win32/getopt.c)
  target_compile_definitions(third_party_lib PUBLIC -DSTATIC_GETOPT)
endif()

add_library(third_party_lib STATIC ${third_party_source_files})

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
  PRIVATE ${CMAKE_BINARY_DIR} . SYSTEM INTERFACE .)

target_link_libraries(third_party_lib PRIVATE standard_settings)
target_link_libraries(third_party_lib INTERFACE blake3)

# These warnings are enabled by default even without e.g. -Wall, but we don't
# want them in third_party.
if(CMAKE_CXX_COMPILER_ID MATCHES "^GNU|Clang$")
  target_compile_options(
    third_party_lib
    PRIVATE
      $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration
      -Wno-int-conversion>)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(
    third_party_lib
    PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-attributes>)
endif()

# Silence warning from winbase.h due to /Zc:preprocessor.
if(MSVC)
  target_compile_options(
    third_party_lib
    PRIVATE /wd5105)
endif()

# The headers are included from the rest of the project, so turn off warnings as
# required.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(third_party_lib INTERFACE -Wno-shadow)
endif()

add_subdirectory(blake3)
