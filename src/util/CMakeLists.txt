set(
  sources
  Bytes.cpp
  LockFile.cpp
  LongLivedLockFileManager.cpp
  TextTable.cpp
  TimePoint.cpp
  Tokenizer.cpp
  UmaskScope.cpp
  environment.cpp
  file.cpp
  path.cpp
  process.cpp
  string.cpp
  zstd.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
