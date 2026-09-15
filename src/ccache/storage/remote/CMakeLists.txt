set(
  sources
  client.cpp
  credentials.cpp
  filestorage.cpp
  helper.cpp
  httptransport.hpp
  remotestorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources redisstorage.cpp)
endif()

if(HTTP_STORAGE_BACKEND)
  list(APPEND sources httpstorage.cpp)
endif()

if(OCI_STORAGE_BACKEND)
  list(APPEND sources ocistorage.cpp)
endif()

if(GHA_STORAGE_BACKEND)
  list(APPEND sources ghastorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
