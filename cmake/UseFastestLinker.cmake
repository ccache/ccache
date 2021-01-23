# Calls `message(VERBOSE msg)` if and only if VERBOSE is available (since CMake 3.15).
# Call CMake with --loglevel=VERBOSE to view those messages.
function(message_verbose msg)
  if(NOT ${CMAKE_VERSION} VERSION_LESS "3.15")
    message(VERBOSE ${msg})
  endif()
endfunction()

function(use_fastest_linker)
  if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(WARNING "use_fastest_linker() disabled, as it is not called at the project top level")
    return()
  endif()
 
  find_program(FASTER_LINKER ld.lld)
  if(NOT FASTER_LINKER)
    find_program(FASTER_LINKER ld.gold)
  endif()
 
  if(FASTER_LINKER)
    # Note: Compiler flag -fuse-ld requires gcc 9 or clang 3.8.
    #       Instead override CMAKE_CXX_LINK_EXECUTABLE directly.
    #       By default CMake uses the compiler executable for linking.
    set(CMAKE_CXX_LINK_EXECUTABLE "${FASTER_LINKER} <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    message_verbose("Using ${FASTER_LINKER} linker for faster linking")
  else()
    message_verbose("Using default linker")
  endif()
endfunction()

option(USE_FASTER_LINKER "Use the lld or gold linker instead of the default for faster linking" TRUE)
if(USE_FASTER_LINKER)
  use_fastest_linker()
endif()
