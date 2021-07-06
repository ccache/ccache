set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/FileStorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/HttpStorage.cpp
)

target_sources(ccache_lib PRIVATE ${sources})
