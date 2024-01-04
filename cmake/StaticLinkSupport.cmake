# This sets up support for maximum static linking of dependent libraries on
# supported platforms.

set(STATIC_LINK_DEFAULT OFF)
if(WIN32)
  set(STATIC_LINK_DEFAULT ON)
endif()

option(STATIC_LINK "Link most libraries statically" ${STATIC_LINK_DEFAULT})

if(NOT STATIC_LINK)
  return()
endif()

set(CMAKE_LINK_SEARCH_START_STATIC ON)
set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(WIN32)
  # Link MSVC runtime statically.
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Link MINGW runtime statically.
  else()
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)\$")
      list(APPEND CCACHE_EXTRA_LIBS -static-libgcc -static-libstdc++ -static -lwinpthread -dynamic)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      list(APPEND CCACHE_EXTRA_LIBS -fuse-ld=lld)
    endif()
  endif()
else()
  if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)\$")
    list(APPEND CCACHE_EXTRA_LIBS -static-libgcc -static-libstdc++)
  endif()
endif()
