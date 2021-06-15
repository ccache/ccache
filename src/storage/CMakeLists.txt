add_subdirectory(primary)

set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/Storage.cpp
)

target_sources(ccache_lib PRIVATE ${sources})
