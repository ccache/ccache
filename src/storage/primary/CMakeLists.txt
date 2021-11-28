set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/CacheFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PrimaryStorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PrimaryStorage_cleanup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PrimaryStorage_compress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PrimaryStorage_statistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StatsFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
