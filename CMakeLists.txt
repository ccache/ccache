cmake_minimum_required(VERSION 3.15)

project(ccache LANGUAGES C CXX ASM ASM_MASM)
if(MSVC)
  enable_language(ASM_MASM)
else()
  enable_language(ASM)
endif()
set(CMAKE_PROJECT_DESCRIPTION "a fast C/C++ compiler cache")

set(CMAKE_CXX_STANDARD 17)
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

if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.8)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
  message(
    FATAL_ERROR
    "The compiler you are using is too old, sorry.\n"
    "You need one listed here: https://ccache.dev/platform-compiler-language-support.html")
endif()

if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4)
    OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6))
  message(
    WARNING
    "The compiler you are using is rather old.\n"
    "If anything goes wrong you might be better off with one listed here:"
    " https://ccache.dev/platform-compiler-language-support.html")

  # Warnings from old compilers are probably useless anyway.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" FALSE)
endif()

#
# Settings
#
include(CcacheVersion)

if(NOT DEFINED CCACHE_DEV_MODE)
  if("${CCACHE_VERSION_ORIGIN}" STREQUAL git OR DEFINED ENV{CI})
    set(CCACHE_DEV_MODE ON)
  else()
    set(CCACHE_DEV_MODE OFF)
  endif()
endif()
message(STATUS "Ccache dev mode: ${CCACHE_DEV_MODE}")

option(ENABLE_IPO "Enable interprocedural (link time, LTO) optimization" OFF)
if(ENABLE_IPO AND NOT MINGW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

include(CIBuildType)
include(DefaultBuildType)
include(UseFastestLinker)
include(StaticLinkSupport)
include(StandardSettings)
include(StandardWarnings)
include(EnableCcache)

#
# Configuration
#

include(InstallDirs)
include(GenerateConfigurationFile)
include(GenerateVersionFile)

#
# Third party
#

option(USE_SYSTEM_LIBRARIES "Use system libraries by default" OFF)

if(USE_SYSTEM_LIBRARIES)
  find_package(PkgConfig REQUIRED)
else()
  find_package(PkgConfig)
endif()

option(USE_SYSTEM_FMT "Use system fmt library" ${USE_SYSTEM_LIBRARIES})
if(USE_SYSTEM_FMT)
  find_package(fmt REQUIRED)
else()
   set(FMT_INSTALL OFF)
   set(FMT_MODULE OFF)
   set(FMT_OS OFF)
   add_subdirectory(src/third_party/fmt)
endif()

option(USE_SYSTEM_EXPECTED "Use system tl-expected library" ${USE_SYSTEM_LIBRARIES})
if(USE_SYSTEM_EXPECTED)
  find_package(tl-expected REQUIRED)
else()
   add_library(tl-expected INTERFACE)
   target_include_directories(tl-expected INTERFACE src/third_party)
   add_library(tl::expected ALIAS tl-expected)
endif()

option(USE_SYSTEM_XXHASH "Use system xxhash library" ${USE_SYSTEM_LIBRARIES})
if(USE_SYSTEM_XXHASH)
  pkg_check_modules(xxhash REQUIRED IMPORTED_TARGET libxxhash)
else()
  add_library(xxhash src/third_party/xxhash.c)
  add_library(PkgConfig::xxhash ALIAS xxhash)
endif()

set(ZSTD_FROM_INTERNET AUTO CACHE STRING "Download and use libzstd from the Internet")
set_property(CACHE ZSTD_FROM_INTERNET PROPERTY STRINGS AUTO ON OFF)

set(HIREDIS_FROM_INTERNET AUTO CACHE STRING "Download and use libhiredis from the Internet")
set_property(CACHE HIREDIS_FROM_INTERNET PROPERTY STRINGS AUTO ON OFF)

option(
  OFFLINE "Do not download anything from the internet"
  "$(FETCHCONTENT_FULLY_DISCONNECTED}"
)
if(OFFLINE)
  set(FETCHCONTENT_FULLY_DISCONNECTED ON)
  set(ZSTD_FROM_INTERNET OFF)
  set(HIREDIS_FROM_INTERNET OFF)
endif()

find_package(zstd 1.1.2 MODULE REQUIRED)

option(REDIS_STORAGE_BACKEND "Enable Redis remote storage" ON)

if(REDIS_STORAGE_BACKEND)
  find_package(hiredis 0.13.3 MODULE REQUIRED)
endif()

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
target_link_libraries(ccache PRIVATE standard_settings standard_warnings ccache_framework)

#
# Documentation
#
option(ENABLE_DOCUMENTATION "Enable documentation" ON)
if(ENABLE_DOCUMENTATION)
  add_subdirectory(doc)
endif()

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
