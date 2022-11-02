set(
  sources
  FileStorage.cpp
  HttpStorage.cpp
  RemoteStorage.cpp
)

if(REDIS_STORAGE_BACKEND)
  list(APPEND sources RedisStorage.cpp)
endif()

if(RPC_STORAGE_BACKEND)
  list(APPEND sources RpcStorage.cpp)
endif()

file(GLOB headers *.hpp)
list(APPEND sources ${headers})

target_sources(ccache_framework PRIVATE ${sources})

if(RPC_STORAGE_BACKEND)
add_executable(rpc-server RpcStorage_server.cpp)

target_link_libraries(
  rpc-server
  PRIVATE standard_settings standard_warnings ccache_framework third_party)
endif()
