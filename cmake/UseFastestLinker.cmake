if(NOT CCACHE_DEV_MODE)
  # For ccache, using a faster linker is in practice only relevant to reduce the
  # compile-link-test cycle for developers, so use the standard linker for
  # non-developer builds.
  return()
endif()

if(MSVC)
  message(STATUS "Using standard linker for MSVC")
  return()
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
  # Be conservative and only probe for a faster linker on platforms that likely
  # don't have toolchain bugs. See for example
  # <https://www.sourceware.org/bugzilla/show_bug.cgi?id=22838>.
  message(STATUS "Not probing for faster linker on ${CMAKE_SYSTEM_PROCESSOR}")
  return()
endif()

function(check_linker linker)
  string(TOUPPER ${linker} upper_linker)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CMakefiles/CMakeTmp/main.c" "int main() { return 0; }")
  try_compile(
    HAVE_LD_${upper_linker}
    ${CMAKE_CURRENT_BINARY_DIR}
    "${CMAKE_CURRENT_BINARY_DIR}/CMakefiles/CMakeTmp/main.c"
    LINK_LIBRARIES "-fuse-ld=${linker}"
  )
endfunction()

function(use_fastest_linker)
  if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(WARNING "use_fastest_linker() disabled, as it is not called at the project top level")
    return()
  endif()

  set(use_default_linker 1)
  check_linker(lld)
  if(HAVE_LD_LLD)
    link_libraries("-fuse-ld=lld")
    set(use_default_linker 0)
    message(STATUS "Using lld linker")
  else()
    check_linker(gold)
    if(HAVE_LD_GOLD)
      link_libraries("-fuse-ld=gold")
      set(use_default_linker 0)
      message(STATUS "Using gold linker")
    endif()
  endif()
  if(use_default_linker)
    message(STATUS "Using default linker")
  endif()
endfunction()

option(USE_FASTER_LINKER "Use the lld or gold linker instead of the default for faster linking" TRUE)
if(USE_FASTER_LINKER)
  use_fastest_linker()
endif()
