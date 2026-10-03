set(
  sources
  client.cpp
  filestorage.cpp
  helper.cpp
  remotestorage.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
