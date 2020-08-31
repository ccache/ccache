# Determine VERSION from a Git repository. VERSION_ERROR is set to a non-empty
# string on error.
function(get_version_from_git)
  set(VERSION_ERROR "" PARENT_SCOPE)

  find_package(Git)
  if(NOT GIT_FOUND)
    set(VERSION_ERROR "Git not found" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND git describe --exact-match
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE git_tag
    ERROR_VARIABLE git_tag_error # silence error
    RESULT_VARIABLE cmd_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(cmd_result EQUAL 0)
    set(VERSION ${git_tag} PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE git_branch OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE git_branch_error
    RESULT_VARIABLE cmd_branch_result)
  if(NOT cmd_branch_result EQUAL 0)
    set(VERSION_ERROR "Failed to run Git: ${git_branch_error}" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND git rev-parse --short=8 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE git_hash OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE git_hash_error
    RESULT_VARIABLE cmd_hash_result)
  if(NOT cmd_hash_result EQUAL 0)
    set(VERSION_ERROR "Failed to run Git: ${git_hash_error}" PARENT_SCOPE)
    return()
  endif()

  set(VERSION "${git_branch}.${git_hash}" PARENT_SCOPE)
endfunction()

set(version_file ${CMAKE_SOURCE_DIR}/VERSION)

if(EXISTS ${version_file})
  file(READ ${version_file} VERSION)
  string(STRIP ${VERSION} VERSION)
else()
  get_version_from_git()
  if(NOT ${VERSION_ERROR} STREQUAL "")
    message(SEND_ERROR "Cannot determine ccache version: ${VERSION_ERROR}")
  endif()
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/version.cpp.in
  ${CMAKE_BINARY_DIR}/src/version.cpp
  @ONLY)

message(STATUS "Ccache version: ${VERSION}")
