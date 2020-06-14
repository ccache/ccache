# Building upon same version as llvm. Let's assume they did their homework and
# it's a good version. Compare:
# ~~~
# https://repology.org/badge/vertical-allrepos/cmake.svg?minversion=3.4.3
# ~~~
cmake_minimum_required(VERSION 3.4.3)

project(ccache LANGUAGES C CXX)
set(CMAKE_PROJECT_DESCRIPTION "a fast C/C++ compiler cache")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS NO)

# Always export compile_commands.json as it's useful for so many tools.
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
include(GenerateConfigurationFile)
include(GenerateVersionFile)

#
# 3rd party
#
option(USE_LIBZSTD_FROM_INTERNET "Download and use libzstd from the Internet"
       OFF)
find_package(zstd 1.1.2 REQUIRED)

option(USE_LIBB2_FROM_INTERNET "Download and use libb2 from the Internet" OFF)
find_package(libb2 0.98 REQUIRED)

#
# special flags
#
# Note: cppcheck will scan everything after this point. Zstd and libb2 are above
# so they don't get scanned.
#
include(CodeAnalysis)
option(ENABLE_TRACING "Enable possibility to use internal ccache tracing" OFF)

#
# ccache_lib
#
add_subdirectory(src)

#
# ccache
#
add_executable(ccache src/main.cpp)
target_link_libraries(ccache PRIVATE standard_settings standard_warnings
                                     ccache_lib)

add_subdirectory(doc)

#
# installation
#
include(GNUInstallDirs)
install(TARGETS ccache DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# packaging
#
include(CCachePackConfig)

#
# test and unittest
#
option(ENABLE_TESTING "Enable Tests" ON)
if(ENABLE_TESTING)
  enable_testing()
  add_subdirectory(unittest)
  add_subdirectory(test)

  # Note: VERSION_GREATER_EQUAL requires cmake 3.17
  if(NOT ${CMAKE_VERSION} VERSION_LESS "3.17")
    list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
  endif()

  # Add 'check' target which will compile & run tests
  if(CMAKE_CONFIGURATION_TYPES)
    add_custom_target(
      check
      COMMAND ${CMAKE_CTEST_COMMAND} --force-new-ctest-process
              --output-on-failure --build-config "$<CONFIGURATION>"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS ccache unittest)
  else()
    add_custom_target(
      check
      COMMAND ${CMAKE_CTEST_COMMAND} --force-new-ctest-process
              --output-on-failure
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS ccache unittest)
  endif()
endif()

#
# Special formatting targets. You can also run the scripts without cmake/make!
#

find_program(CLANG_FORMAT_EXE NAMES "clang-format"
             DOC "Path to clang-format executable")
mark_as_advanced(CLANG_FORMAT_EXE) # don't show in ccmake

if(NOT CLANG_FORMAT_EXE)
  message(WARNING "clang-format not found.")
else()
  add_custom_target(
    format
    COMMAND misc/format.sh
    COMMENT "Formatting code"
    USES_TERMINAL
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_custom_target(
    check_format
    COMMAND misc/check_format.sh
    COMMENT "Checking correctly formatted code"
    USES_TERMINAL
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
