cmake_minimum_required(VERSION 3.4.3)

project(ccache LANGUAGES ASM C CXX)
set(CMAKE_PROJECT_DESCRIPTION "a fast C/C++ compiler cache")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS NO)

# Always export compile_commands.json since it's useful for many tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Settings
#
include(StandardSettings)
include(StandardWarnings)
include(DefaultBuildType)

#
# Configuration
#
include(GNUInstallDirs)
include(GenerateConfigurationFile)
include(GenerateVersionFile)

if(HAVE_SYS_MMAN_H)
  set(INODE_CACHE_SUPPORTED 1)
endif()

#
# Third party
#
option(ZSTD_FROM_INTERNET "Download and use libzstd from the Internet" OFF)
find_package(zstd 1.1.2 REQUIRED)

#
# Special flags
#
# Note: Cppcheck will scan everything after this point. zstd is above so it
# doesn't get scanned.
#
include(CodeAnalysis)
option(ENABLE_TRACING "Enable possibility to use internal ccache tracing" OFF)

#
# Source code
#
add_subdirectory(src)

#
# ccache executable
#
add_executable(ccache src/main.cpp)
target_link_libraries(ccache PRIVATE standard_settings standard_warnings ccache_lib)

#
# Documentation
#
add_subdirectory(doc)

#
# Installation
#
install(TARGETS ccache DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Packaging
#
include(CCachePackConfig)

#
# Tests
#
option(ENABLE_TESTING "Enable tests" ON)
if(ENABLE_TESTING)
  enable_testing()
  add_subdirectory(unittest)
  add_subdirectory(test)

  # Note: VERSION_GREATER_EQUAL requires CMake 3.17
  if(NOT ${CMAKE_VERSION} VERSION_LESS "3.17")
    list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
  endif()

  # Add "check" target which compiles and runs tests.
  set(
    check_command
    ${CMAKE_CTEST_COMMAND} --force-new-ctest-process --output-on-failure)
  if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND check_command --build-config "$<CONFIGURATION>")
  endif()
  add_custom_target(
    check
    COMMAND ${check_command}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ccache unittest)
endif()

#
# Special formatting targets
#
find_program(
  CLANG_FORMAT_EXE
  NAMES "clang-format"
  DOC "Path to clang-format executable.")
mark_as_advanced(CLANG_FORMAT_EXE) # Don't show in CMake UIs

if(NOT CLANG_FORMAT_EXE)
  message(NOTICE "clang-format not found")
else()
  add_custom_target(
    format
    COMMAND misc/format-files --all
    COMMENT "Formatting code"
    USES_TERMINAL
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_custom_target(
    check_format
    COMMAND misc/format-files --all --check
    COMMENT "Checking code formatting"
    USES_TERMINAL
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
