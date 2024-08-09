set(
  sources
  assertions.cpp
  bytes.cpp
  cpu.cpp
  direntry.cpp
  environment.cpp
  error.cpp
  file.cpp
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
  timepoint.cpp
  umaskscope.cpp
  zstd.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
