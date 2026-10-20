set(
  sources
  clang.cpp
  headersearch.cpp
  msvc.cpp
)

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})
