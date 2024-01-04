set(
  sources
  LocalStorage.cpp
  StatsFile.cpp
  util.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
