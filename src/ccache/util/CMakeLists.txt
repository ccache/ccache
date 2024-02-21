set(
  sources
  Bytes.cpp
  DirEntry.cpp
  LockFile.cpp
  LongLivedLockFileManager.cpp
  MemoryMap.cpp
  TemporaryFile.cpp
  TextTable.cpp
  ThreadPool.cpp
  TimePoint.cpp
  Tokenizer.cpp
  UmaskScope.cpp
  assertions.cpp
  environment.cpp
  error.cpp
  file.cpp
  filesystem.cpp
  logging.cpp
  path.cpp
  process.cpp
  string.cpp
  time.cpp
  zstd.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
