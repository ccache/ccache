add_subdirectory(socketbackend)

set(
  sources
  filestorage.cpp
  remotestorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources redisstorage.cpp)
endif()

if(HTTP_STORAGE_BACKEND)
  list(APPEND sources httpstorage.cpp)
endif()

if(EXPERIMENTAL_STORAGE_BACKEND)
  list(APPEND sources socketstorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
