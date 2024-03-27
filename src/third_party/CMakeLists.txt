add_library(third_party STATIC xxhash.c)
target_include_directories(third_party INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

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

# Silence warning from winbase.h due to /Zc:preprocessor.
if(MSVC)
  target_compile_options(third_party PRIVATE /wd5105)
endif()

add_subdirectory(blake3)
add_subdirectory(cxxurl)
add_subdirectory(win32-compat)

if(NOT TARGET dep_cpphttplib)
  add_subdirectory(cpp-httplib)
endif()
if(NOT TARGET dep_nonstdspan)
  add_subdirectory(nonstd-span)
endif()
if(NOT TARGET dep_tlexpected)
  add_subdirectory(tl-expected)
endif()
