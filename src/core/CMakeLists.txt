set(
  sources
  CacheEntryHeader.cpp
  CacheEntryReader.cpp
  CacheEntryWriter.cpp
  Manifest.cpp
  Statistics.cpp
  StatisticsCounters.cpp
  StatsLog.cpp
  mainoptions.cpp
  types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
