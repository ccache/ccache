set(
  sources
  args.cpp
  assertions.cpp
  bytes.cpp
  clang.cpp
  cpu.cpp
  direntry.cpp
  environment.cpp
  error.cpp
  exec.cpp
  file.cpp
  filelock.cpp
  filesystem.cpp
  lockfile.cpp
  logging.cpp
  longlivedlockfilemanager.cpp
  memorymap.cpp
  path.cpp
  process.cpp
  string.cpp
  temporaryfile.cpp
  texttable.cpp
  threadpool.cpp
  time.cpp
  tokenizer.cpp
  umaskscope.cpp
  zstd.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
