set(
  sources
  Bytes.cpp
  LockFile.cpp
  TextTable.cpp
  TimePoint.cpp
  Tokenizer.cpp
  file.cpp
  path.cpp
  string.cpp
  zstd.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
