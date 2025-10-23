set(
  sources
  client.cpp
  filestorage.cpp
  remotestorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources redisstorage.cpp)
endif()

if(HTTP_STORAGE_BACKEND)
  list(APPEND sources httpstorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
