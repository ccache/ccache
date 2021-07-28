set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/FileStorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/HttpStorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SecondaryStorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/RedisStorage.cpp)
endif()

target_sources(ccache_framework PRIVATE ${sources})
