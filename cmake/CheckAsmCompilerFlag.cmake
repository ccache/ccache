include(CMakeCheckCompilerFlagCommonPatterns)

function(check_asm_compiler_flag flag var)
  if(DEFINED "${var}")
    return()
  endif()

  set(locale_vars LC_ALL LC_MESSAGES LANG)
  foreach(v IN LISTS locale_vars)
    set(locale_vars_saved_${v} "$ENV{${v}}")
    set(ENV{${v}} C)
  endforeach()

  check_compiler_flag_common_patterns(common_patterns)

  set(test_file "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.S")
  file(WRITE "${test_file}" ".global main\nmain:\n")

  if(NOT CMAKE_REQUIRED_QUIET)
    message(STATUS "Performing Test ${var}")
  endif()
  try_compile(
    ${var}
    "${CMAKE_BINARY_DIR}"
    "${test_file}"
    COMPILE_DEFINITIONS "${flag}"
    OUTPUT_VARIABLE output)

  check_compiler_flag_common_patterns(common_fail_patterns)

  foreach(regex ${common_fail_patterns})
    if("${output}" MATCHES "${regex}")
      set(${var} 0)
    endif()
  endforeach()

  if(${${var}})
    set(${var} 1 CACHE INTERNAL "Test ${var}")
    if(NOT CMAKE_REQUIRED_QUIET)
      message(STATUS "Performing Test ${var} - Success")
    endif()
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Performing ASM SOURCE FILE Test ${var} succeeded with the following output:\n"
      "${output}\n"
      "Source file was:\n${test_file}\n")
  else()
    if(NOT CMAKE_REQUIRED_QUIET)
      message(STATUS "Performing Test ${var} - Failed")
    endif()
    set(${var} "" CACHE INTERNAL "Test ${var}")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Performing ASM SOURCE FILE Test ${var} failed with the following output:\n"
      "${output}\n"
      "Source file was:\n${test_file}\n")
  endif()

  foreach(v IN LISTS locale_vars)
    set(ENV{${v}} ${locale_vars_saved_${v}})
  endforeach()

  set(${var} "${${var}}" PARENT_SCOPE)
endfunction()
