# Determines VERSION from git. See also VERSION_ERROR and VERSION_DIRTY.
function(get_version_from_git)
  find_package(Git)
  if(NOT GIT_FOUND)
    message(STATUS "Git not found")
    set(VERSION_ERROR TRUE PARENT_SCOPE)
    set(VERSION_DIRTY TRUE PARENT_SCOPE)
    set(VERSION "unknown" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND git describe --exact-match
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE git_tag
    ERROR_VARIABLE git_tag
    RESULT_VARIABLE cmd_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(cmd_result EQUAL 0)
    set(VERSION_ERROR FALSE PARENT_SCOPE)
    set(VERSION_DIRTY FALSE PARENT_SCOPE)
    set(VERSION ${git_tag} PARENT_SCOPE)
  else()
    execute_process(
      COMMAND git rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE git_branch OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE cmd_branch_result)

    execute_process(
      COMMAND git rev-parse --short=8 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE git_hash OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE cmd_hash_result)

    if(cmd_branch_result EQUAL 0 AND cmd_hash_result EQUAL 0)
      set(VERSION_ERROR FALSE PARENT_SCOPE)
      set(VERSION_DIRTY TRUE PARENT_SCOPE)
      set(VERSION "${git_branch}.${git_hash}" PARENT_SCOPE)
    else()
      message(WARNING "Running git failed")
      set(VERSION_ERROR TRUE PARENT_SCOPE)
      set(VERSION_DIRTY TRUE PARENT_SCOPE)
      set(VERSION "unknown" PARENT_SCOPE)
    endif()
  endif()
endfunction()

get_version_from_git()
if(VERSION_ERROR)
  message(STATUS "Not within git repository")
else()
  configure_file(${CMAKE_SOURCE_DIR}/cmake/Version.cpp.in
                 ${CMAKE_SOURCE_DIR}/src/Version.cpp @ONLY)
endif()
