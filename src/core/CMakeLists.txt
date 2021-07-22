set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/Statistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StatisticsCounters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StatsLog.cpp
)

target_sources(ccache_lib PRIVATE ${sources})
