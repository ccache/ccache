set(
  sources
  atomicfile.cpp
  cacheentry.cpp
  common.cpp
  filerecompressor.cpp
  mainoptions.cpp
  manifest.cpp
  msvcshowincludesoutput.cpp
  result.cpp
  resultextractor.cpp
  resultinspector.cpp
  resultretriever.cpp
  statistics.cpp
  statisticscounters.cpp
  statslog.cpp
  types.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
