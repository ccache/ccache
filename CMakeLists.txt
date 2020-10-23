cmake_minimum_required(VERSION 3.4.3)

project(ccache LANGUAGES C CXX ASM)
set(CMAKE_PROJECT_DESCRIPTION "a fast C/C++ compiler cache")

if(NOT "${CMAKE_CXX_STANDARD}")
  set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS NO)

# Always export compile_commands.json since it's useful for many tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Minimum compiler requirements (fail gracefully instead of producing cryptic
# C++ error messages)
#

# Clang 3.4 and AppleClang 6.0 fail to compile doctest.
# GCC 4.8.4 is known to work OK but give warnings.
if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.5)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.4)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
  message(
    FATAL_ERROR
    "The compiler you are using is too old, sorry.\n"
    "You need one listed here: https://ccache.dev/platform-compiler-language-support.html")
endif()

# All Clang problems / special handling ccache has are because of version 3.5.
# All GCC problems / special handling ccache has are because of version 4.8.4.
if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.6)
    OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0))
  message(
    WARNING
    "The compiler you are using is rather old.\n"
    "If anything goes wrong you might be better off with one listed here:"
    " https://ccache.dev/platform-compiler-language-support.html")
endif()

#
# Settings
#
include(StandardSettings)
include(StandardWarnings)
include(CIBuildType)
include(DefaultBuildType)

if(NOT ${CMAKE_VERSION} VERSION_LESS "3.9")
  cmake_policy(SET CMP0069 NEW)
  option(ENABLE_IPO "Enable interprocedural (link time, LTO) optimization" OFF)
  if(ENABLE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

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
set(ZSTD_FROM_INTERNET_DEFAULT OFF)

# Default to downloading deps for Visual Studio, unless using a package manager.
if(MSVC AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg|conan")
  set(ZSTD_FROM_INTERNET_DEFAULT ON)
endif()

option(ZSTD_FROM_INTERNET "Download and use libzstd from the Internet" ${ZSTD_FROM_INTERNET_DEFAULT})
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
include(CcachePackConfig)

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
  message(STATUS "clang-format not found")
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
