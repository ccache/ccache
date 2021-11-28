set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/CacheEntryHeader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/CacheEntryReader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/CacheEntryWriter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Statistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StatisticsCounters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StatsLog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mainoptions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/types.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
