# This file provides a special "standard_settings" target which is supposed to
# be linked privately by all other targets.

add_library(standard_settings INTERFACE)

# Not supported in CMake 3.4: target_compile_features(project_options INTERFACE
# c_std_11 cxx_std_11)

if(CMAKE_CXX_COMPILER_ID MATCHES "^GNU|(Apple)?Clang$")
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

elseif(MSVC)
  target_compile_options(standard_settings INTERFACE /std:c++latest /Zc:preprocessor /Zc:__cplusplus /D_CRT_SECURE_NO_WARNINGS)
endif()
