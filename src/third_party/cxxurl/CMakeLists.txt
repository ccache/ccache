add_library(
  dep_cxxurl
  STATIC EXCLUDE_FROM_ALL
  "${CMAKE_CURRENT_SOURCE_DIR}/cxxurl/url.cpp"
)
target_include_directories(
  dep_cxxurl
  SYSTEM INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
