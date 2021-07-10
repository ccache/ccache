set(
  sources
  ${CMAKE_CURRENT_SOURCE_DIR}/FileStorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/HttpStorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/RedisStorage.cpp)
endif()

target_sources(ccache_lib PRIVATE ${sources})
