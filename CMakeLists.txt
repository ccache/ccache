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

include(Utils)
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

option(REDIS_STORAGE_BACKEND "Enable Redis remote storage" ON)
option(HTTP_STORAGE_BACKEND "Enable HTTP remote storage" ON)
option(ENABLE_TESTING "Enable tests" ON)

include(InstallDirs)
include(GenerateConfigurationFile)
include(GenerateVersionFile)
include(Dependencies)

#
# Special flags
#
# Note: Cppcheck will scan everything after this point. zstd is above so it
# doesn't get scanned.
#
include(CodeAnalysis)

#
# Source code
#
add_subdirectory(src/third_party)
add_subdirectory(src/ccache)

#
# ccache executable
#
add_executable(ccache src/ccache/main.cpp)
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
if(WIN32)
  install(FILES ${CMAKE_CURRENT_LIST_DIR}/misc/ccache.exe.manifest TYPE BIN DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

#
# Packaging
#
include(CcachePackConfig)

#
# Tests
#
if(ENABLE_TESTING)
  enable_testing()
  add_subdirectory(unittest)
  add_subdirectory(test)

  list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")

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

#
# Configuration summary
#

message(STATUS)
message(STATUS "Configuration summary:")
message(STATUS "  Storage backends:")
message(STATUS "    file  ON")
message(STATUS "    http  ${HTTP_STORAGE_BACKEND}")
message(STATUS "    redis ${REDIS_STORAGE_BACKEND}")
message(STATUS "  Dependencies:")
print_dependency_summary("    ")
message(STATUS)
