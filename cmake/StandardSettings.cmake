# This file provides a special "standard_settings" target which is supposed to
# be linked privately by all other targets.

add_library(standard_settings INTERFACE)

if(MSVC)
  target_compile_options(
    standard_settings
    INTERFACE
      $<$<COMPILE_LANGUAGE:C>:-include /FI${CMAKE_BINARY_DIR}/config.h>
      $<$<COMPILE_LANGUAGE:CXX>:-include /FI${CMAKE_BINARY_DIR}/config.h>
  )
else()
  target_compile_options(
    standard_settings
    INTERFACE
      $<$<COMPILE_LANGUAGE:C>:-include ${CMAKE_BINARY_DIR}/config.h>
      $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_BINARY_DIR}/config.h>
  )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "^GNU|(Apple)?Clang$" AND NOT MSVC)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(standard_settings INTERFACE _GLIBCXX_ASSERTIONS)
  endif()

  option(ENABLE_COVERAGE "Enable coverage reporting for GCC/Clang" FALSE)
  if(ENABLE_COVERAGE)
    target_compile_options(standard_settings INTERFACE --coverage -O0 -g)
    target_link_libraries(standard_settings INTERFACE --coverage)
  endif()

  set(SANITIZERS "")

  option(ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" FALSE)
  if(ENABLE_SANITIZER_ADDRESS)
    list(APPEND SANITIZERS "address")
  endif()

  option(ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" FALSE)
  if(ENABLE_SANITIZER_MEMORY)
    list(APPEND SANITIZERS "memory")
  endif()

  option(
    ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
    "Enable undefined behavior sanitizer"
    FALSE)
  if(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR)
    list(APPEND SANITIZERS "undefined")
  endif()

  option(ENABLE_SANITIZER_THREAD "Enable thread sanitizer" FALSE)
  if(ENABLE_SANITIZER_THREAD)
    list(APPEND SANITIZERS "thread")
  endif()

  foreach(SANITIZER IN LISTS SANITIZERS)
    target_compile_options(
      standard_settings
      INTERFACE -fsanitize=${SANITIZER})
    target_link_libraries(
      standard_settings
      INTERFACE -fsanitize=${SANITIZER})
  endforeach()

  include(StdAtomic)
elseif(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(standard_settings INTERFACE
      /Zc:__cplusplus
      $<$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,19.25>:/Zc:preprocessor>
      /utf-8
  )
endif()

if(WIN32)
  target_compile_definitions(
    standard_settings
    INTERFACE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
  )
endif()
