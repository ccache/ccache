set(
  sources
  FileStorage.cpp
  HttpStorage.cpp
  RemoteStorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources RedisStorage.cpp)
endif()

target_sources(ccache_framework PRIVATE ${sources})
