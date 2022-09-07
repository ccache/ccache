set(
  sources
  Compressor.cpp
  Decompressor.cpp
  NullCompressor.cpp
  NullDecompressor.cpp
  ZstdCompressor.cpp
  ZstdDecompressor.cpp
  types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
