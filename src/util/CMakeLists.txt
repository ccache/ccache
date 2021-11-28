set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/TextTable.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Tokenizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/file.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/string.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
