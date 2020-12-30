add_library(ccache_compression
  src/Compression.cpp
  src/Compressor.cpp
  src/Decompressor.cpp
  src/NullCompressor.cpp
  src/NullDecompressor.cpp
  src/ZstdCompressor.cpp
  src/ZstdDecompressor.cpp
)

target_include_directories(ccache_compression PUBLIC include)

target_link_libraries(ccache_compression PRIVATE ccache_core)
