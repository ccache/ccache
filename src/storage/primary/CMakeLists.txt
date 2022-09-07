set(
  sources
  CacheFile.cpp
  PrimaryStorage.cpp
  PrimaryStorage_cleanup.cpp
  PrimaryStorage_compress.cpp
  PrimaryStorage_statistics.cpp
  StatsFile.cpp
  util.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
