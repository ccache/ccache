# Note: Compiling ccache via ccache is fine, because this uses a stable version which
# is installed on the system.


# Calls `message(VERBOSE msg)` if and only if VERBOSE is available (since CMake 3.15).
# Call CMake with --loglevel=VERBOSE to view those messages.
function(message_verbose msg)
  if(NOT ${CMAKE_VERSION} VERSION_LESS "3.15")
    message(VERBOSE ${msg})
  endif()
endfunction()

# Modified version of Craig Scott's "Professional CMake: A Practical Guide", 8th Edition
function(use_ccache)
  if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(WARNING "use_ccache() disabled, as it is not called from the project top level")
    return()
  endif()

  find_program(CCACHE_PROGRAM ccache)
  if(NOT CCACHE_PROGRAM)
    message_verbose("Ccache program not found, not enabling ccache for faster recompilation")
    return()
  endif()

  message_verbose("Ccache enabled for faster recompilation")

  # This will override any config and environment settings.
  # Worst case it's overriding better suited user defined values.
  set(ccacheEnv
    # Another option would be CMAKE_BINARY_DIR, however currently only one basedir is supported.
    CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}

    # In case of very old ccache versions (pre 3.3)
    CCACHE_CPP2=true

    # This has been turned on by default in ccache 4.0
    # CCACHE_COMPRESS=1
  )

  if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    foreach(lang IN ITEMS C CXX OBJC OBJCXX CUDA)
      set(CMAKE_${lang}_COMPILER_LAUNCHER
        ${CMAKE_COMMAND} -E env ${ccacheEnv} ${CCACHE_PROGRAM}
        PARENT_SCOPE)
    endforeach()
  elseif(CMAKE_GENERATOR STREQUAL Xcode)
    foreach(lang IN ITEMS C CXX)
      set(launcher ${CMAKE_BINARY_DIR}/launch-${lang})
      file(WRITE ${launcher} "#!/bin/bash\n\n")
      foreach(keyVal IN LISTS ccacheEnv)
        file(APPEND ${launcher} "export ${keyVal}\n")
      endforeach()
      file(APPEND ${launcher}
        "exec \"${CCACHE_PROGRAM}\" \"${CMAKE_${lang}_COMPILER}\" \"$@\"\n")
      execute_process(COMMAND chmod a+rx ${launcher})
    endforeach()
    set(CMAKE_XCODE_ATTRIBUTE_CC ${CMAKE_BINARY_DIR}/launch-C PARENT_SCOPE)
    set(CMAKE_XCODE_ATTRIBUTE_CXX ${CMAKE_BINARY_DIR}/launch-CXX PARENT_SCOPE)
    set(CMAKE_XCODE_ATTRIBUTE_LD ${CMAKE_BINARY_DIR}/launch-C PARENT_SCOPE)
    set(CMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS ${CMAKE_BINARY_DIR}/launch-CXX PARENT_SCOPE)
  endif()
endfunction()

option(USE_CCACHE "Use ccache to speed up recompilation time" TRUE)
if(USE_CCACHE)
  use_ccache()
endif()
