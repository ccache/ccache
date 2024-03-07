set(
  sources
  FileStorage.cpp
  HttpStorage.cpp
  RemoteStorage.cpp
)

if(REDIS_STORAGE_BACKEND OR REDISS_STORAGE_BACKEND)
  list(APPEND sources RedisStorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
