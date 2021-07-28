add_subdirectory(primary)
add_subdirectory(secondary)

set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/Storage.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
