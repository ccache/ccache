# Note: Compiling ccache via ccache is fine because the ccache version installed
# in the system is used.

if(MSVC)
  # Ccache does not support cl.exe-style arguments at this time.
  return()
endif()

option(USE_CCACHE "Use ccache to speed up recompilation time" TRUE)
if(NOT USE_CCACHE)
  return()
endif()

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(WARNING "use_ccache() disabled, as it is not called from the project top level")
  return()
endif()

include(EnableCcache)

find_program(CCACHE_EXECUTABLE ccache)
if(NOT CCACHE_EXECUTABLE)
  ccache_message_verbose("Ccache program not found, not enabling ccache for faster recompilation")
  return()
endif()

enable_ccache()
