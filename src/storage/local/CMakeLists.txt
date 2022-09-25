set(
  sources
  CacheFile.cpp
  LocalStorage.cpp
  LocalStorage_cleanup.cpp
  LocalStorage_compress.cpp
  LocalStorage_statistics.cpp
  StatsFile.cpp
  util.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
