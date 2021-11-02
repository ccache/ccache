set(
  sources
  CacheEntry.cpp
  Manifest.cpp
  Result.cpp
  ResultExtractor.cpp
  ResultInspector.cpp
  ResultRetriever.cpp
  ShowIncludesParser.cpp
  Statistics.cpp
  StatisticsCounters.cpp
  StatsLog.cpp
  mainoptions.cpp
  types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
