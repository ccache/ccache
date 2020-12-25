add_library(ccache_core
  src/assertions.cpp
  src/Util.cpp
)

target_include_directories(ccache_core PUBLIC
  include
  ${CMAKE_BINARY_DIR} # for config.h
)

target_link_libraries(ccache_core PUBLIC third_party_lib)
