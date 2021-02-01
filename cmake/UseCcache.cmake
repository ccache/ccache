# Note: Compiling ccache via ccache is fine because the ccache version installed
# in the system is used.

# Calls `message(VERBOSE msg)` if and only if VERBOSE is available (since CMake
# 3.15). Call CMake with --log-level=VERBOSE to view verbose messages.
function(message_verbose msg)
  if(NOT ${CMAKE_VERSION} VERSION_LESS "3.15")
    message(VERBOSE ${msg})
  endif()
endfunction()

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

  # Note: This will override any config and environment settings.
  set(ccache_env
    # Another option would be CMAKE_BINARY_DIR, but currently only one base
    # directory is supported.
    CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}

    # In case of very old ccache versions (pre 3.3).
    CCACHE_CPP2=true
  )

  if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    find_program(ENV_PROGRAM env)
    if(ENV_PROGRAM)
      set(env_program ${ENV_PROGRAM}) # faster than "cmake -E env"
    else()
      set(env_program ${CMAKE_COMMAND} -E env)
    endif()
    foreach(lang IN ITEMS C CXX OBJC OBJCXX CUDA)
      set(CMAKE_${lang}_COMPILER_LAUNCHER
        ${env_program} ${ccache_env} ${CCACHE_PROGRAM}
        PARENT_SCOPE)
    endforeach()
  elseif(CMAKE_GENERATOR STREQUAL Xcode)
    foreach(lang IN ITEMS C CXX)
      set(launcher ${CMAKE_BINARY_DIR}/launch-${lang})
      file(WRITE ${launcher} "#!/bin/bash\n\n")
      foreach(key_val IN LISTS ccache_env)
        file(APPEND ${launcher} "export ${key_val}\n")
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
