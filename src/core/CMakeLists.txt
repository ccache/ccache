set(
  sources
  CacheEntryHeader.cpp
  CacheEntryReader.cpp
  CacheEntryWriter.cpp
  Manifest.cpp
  Result.cpp
  ResultExtractor.cpp
  ResultInspector.cpp
  ResultRetriever.cpp
  Statistics.cpp
  StatisticsCounters.cpp
  StatsLog.cpp
  mainoptions.cpp
  types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
