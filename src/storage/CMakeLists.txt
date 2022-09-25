add_subdirectory(local)
add_subdirectory(remote)

set(
  sources
  Storage.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
