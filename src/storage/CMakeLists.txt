add_subdirectory(primary)
add_subdirectory(secondary)

set(
  sources
  Storage.cpp
)

target_sources(ccache_framework PRIVATE ${sources})
