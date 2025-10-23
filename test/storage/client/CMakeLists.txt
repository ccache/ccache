add_executable(storage-test-client main.cpp)

target_link_libraries(
  storage-test-client
  PRIVATE
    standard_settings
    ccache_framework
)

# Build by default but don't install.
set_target_properties(storage-test-client PROPERTIES EXCLUDE_FROM_ALL FALSE)
