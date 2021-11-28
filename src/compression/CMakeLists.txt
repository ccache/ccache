set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/Compressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Decompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/NullCompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/NullDecompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ZstdCompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ZstdDecompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
