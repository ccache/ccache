add_subdirectory(local)
add_subdirectory(remote)

set(
  sources
  storage.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
