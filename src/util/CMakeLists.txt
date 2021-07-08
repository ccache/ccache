set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/Tokenizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/file_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/path_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/string_utils.cpp
)

target_sources(ccache_lib PRIVATE ${sources})
