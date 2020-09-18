# This file provides a special "standard_warnings" target which is supposed to
# be linked privately by all product and test code, but not by third party code.
add_library(standard_warnings INTERFACE)

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git" OR DEFINED ENV{"CI"})
  # Enabled by default for development builds and CI builds.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" TRUE)
else()
  # Disabled by default for end user builds so compilation doesn't fail with new
  # compilers that may emit new warnings.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" FALSE)
endif()

include(CheckCXXCompilerFlag)

# check_cxx_compiler_flag caches the result, so a unique variable name is
# required for every flag to be checked.
#
# Parameters:
#
# * flag [in], e.g. FLAG
# * var_name_of_var_name [in], e.g. "TEMP". This is the variable that "HAS_FLAG"
#   will be written to.
function(generate_unique_has_flag_var_name flag var_name_of_var_name)
  string(REGEX REPLACE "[=-]" "_" var_name "${flag}")
  string(TOUPPER "${var_name}" var_name)
  set(${var_name_of_var_name} "HAS_${var_name}" PARENT_SCOPE)
endfunction()

function(add_target_compile_flag_if_supported_ex target flag alternative_flag)
  # has_flag will contain "HAS_$flag" so each flag gets a unique HAS variable.
  generate_unique_has_flag_var_name("${flag}" "has_flag")

  # Instead of passing "has_flag" this passes the content of has_flag.
  check_cxx_compiler_flag("${flag}" "${has_flag}")

  if(${${has_flag}})
    target_compile_options(${target} INTERFACE "${flag}")
  elseif("${alternative_flag}")
    add_target_compile_flag_if_supported_ex(${target} ${alternative_flag} "")
  endif()
endfunction()

# TODO: Is there a better way to provide an optional third argument?
macro(add_target_compile_flag_if_supported target flag)
  add_target_compile_flag_if_supported_ex("${target}" "${flag}" "")
endmacro()

set(CLANG_GCC_WARNINGS
    -Wall
    -Wextra
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wpedantic

    # Candidates for enabling in the future:
    # -Wshadow
    # -Wold-style-cast
    # -Wconversion
    # -Wsign-conversion
    # -Wnull-dereference
    # -Wformat=2
)
# Tested separately as this is not supported by Clang 3.4.
add_target_compile_flag_if_supported(standard_warnings "-Wdouble-promotion")

if(WARNINGS_AS_ERRORS)
  set(CLANG_GCC_WARNINGS ${CLANG_GCC_WARNINGS} -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
    set(
      CLANG_GCC_WARNINGS
      ${CLANG_GCC_WARNINGS}
      -Qunused-arguments
      -Wno-error=unreachable-code)
  endif()

  target_compile_options(
    standard_warnings
    INTERFACE
      ${CLANG_GCC_WARNINGS}
      -Weverything
      -Wno-c++98-compat-pedantic
      -Wno-c++98-compat
      -Wno-constexpr-not-const
      -Wno-conversion
      -Wno-disabled-macro-expansion
      -Wno-documentation-unknown-command
      -Wno-exit-time-destructors
      -Wno-format-nonliteral
      -Wno-global-constructors
      -Wno-implicit-fallthrough
      -Wno-padded
      -Wno-shorten-64-to-32
      -Wno-sign-conversion
      -Wno-weak-vtables
      -Wno-old-style-cast)

  # If compiler supports -Wshadow-field-in-constructor, disable only that.
  # Otherwise disable shadow.
  add_target_compile_flag_if_supported_ex(
    standard_warnings "-Wno-shadow-field-in-constructor" "-Wno-shadow")

  # Disable C++20 compatibility for now.
  add_target_compile_flag_if_supported(standard_warnings "-Wno-c++2a-compat")

  # If compiler supports these warnings they have to be disabled for now.
  add_target_compile_flag_if_supported(
    standard_warnings "-Wno-zero-as-null-pointer-constant")
  add_target_compile_flag_if_supported(
    standard_warnings "-Wno-undefined-func-template")
  add_target_compile_flag_if_supported(
    standard_warnings "-Wno-return-std-move-in-c++11")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(
    standard_warnings
    INTERFACE ${CLANG_GCC_WARNINGS}
    # Warn about logical operations being used where bitwise were probably
    # wanted.
    -Wlogical-op

    # Candidates for enabling in the future:
    # -Wduplicated-cond
    # -Wduplicated-branches
    # -Wuseless-cast
  )

  # TODO: Exact version or reason unknown, discovered in Ubuntu 14 Docker test
  # with GCC 4.8.4
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.5)
    add_target_compile_flag_if_supported(
      standard_warnings "-Wno-missing-field-initializers")
    add_target_compile_flag_if_supported(
      standard_warnings "-Wno-unused-variable")
  endif()
elseif(MSVC)
  # Remove any warning level flags added by CMake.
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}")
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

  target_compile_options(
    standard_warnings
    INTERFACE
    /W4
    # Ignore bad macro in winbase.h triggered by /Zc:preprocessor
    /wd5105
    # Conversion warnings.
    /wd4244
    /wd4267
    # Assignment in conditional.
    /wd4706
    # Non-underscore-prefixed POSIX functions.
    /wd4996
  )
endif()
