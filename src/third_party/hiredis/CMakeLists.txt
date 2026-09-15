register_dependency(Hiredis BUNDLED 1.4.1)

add_library(
  dep_hiredis STATIC EXCLUDE_FROM_ALL
  alloc.c
  async.c
  dict.c
  hiredis.c
  net.c
  read.c
  sds.c
  sockcompat.c
)
if(WIN32)
  target_compile_definitions(dep_hiredis PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(dep_hiredis INTERFACE ws2_32)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/hiredis")
file(GLOB headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(COPY ${headers} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/hiredis")
target_include_directories(
  dep_hiredis SYSTEM INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
)
