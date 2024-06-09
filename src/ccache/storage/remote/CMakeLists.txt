set(
  sources
  filestorage.cpp
  httpstorage.cpp
  remotestorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources redisstorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
