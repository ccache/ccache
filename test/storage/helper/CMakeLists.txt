add_executable(ccache-storage-test main.cpp)

target_link_libraries(
  ccache-storage-test
  PRIVATE
    standard_settings
    ccache_framework
)

# Build by default but don't install.
set_target_properties(ccache-storage-test PROPERTIES EXCLUDE_FROM_ALL FALSE)
