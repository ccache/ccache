set(
  sources
  Bytes.cpp
  LockFile.cpp
  TextTable.cpp
  Tokenizer.cpp
  file.cpp
  path.cpp
  string.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
