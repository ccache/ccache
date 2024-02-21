set(
  sources
  AtomicFile.cpp
  CacheEntry.cpp
  FileRecompressor.cpp
  Manifest.cpp
  MsvcShowIncludesOutput.cpp
  Result.cpp
  ResultExtractor.cpp
  ResultInspector.cpp
  ResultRetriever.cpp
  Statistics.cpp
  StatisticsCounters.cpp
  StatsLog.cpp
  common.cpp
  mainoptions.cpp
  types.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
