# This file provides a special target 'standard_warnings' which shall be linked
# privately by all product and test code, but not by third_party code.
add_library(standard_warnings INTERFACE)

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git" OR DEFINED ENV{"CI"})
  # Enabled by default for builds based on git as this will prevent bad pull
  # requests to ccache repository. Also enabled in case of Travis builds
  # (Environment var CI is set).
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" TRUE)
else()
  # Disabled by default so compilation doesn't fail with new compilers, just
  # because they produce a new warning.
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" FALSE)
endif()

include(CheckCXXCompilerFlag)

# check_cxx_compiler_flag caches the result, so a unique variable name is
# required for every flag to be checked.
#
# * flag [in], e.g. XXX
# * variable_name_of_variable_name [in], e.g. "TEMP". This is the variable that
#   "HAS_XXX" will be written to.
function(generate_unique_has_flag_variable_name flag
         variable_name_of_variable_name)
  string(REGEX REPLACE "[=-]" "_" variable_name "${flag}")
  string(TOUPPER "${variable_name}" variable_name)
  set(${variable_name_of_variable_name} "HAS_${variable_name}" PARENT_SCOPE)
endfunction()

function(add_target_compile_flag_if_supported_ex target flag alternative_flag)
  # has_flag will contain "HAS_$flag" so each flag gets a unique HAS variable.
  generate_unique_has_flag_variable_name("${flag}" "has_flag")

  # Instead of passing "has_flag" this is passing the content of has_flag.
  check_cxx_compiler_flag("${flag}" "${has_flag}")

  # If the variable named in has_flag is true, compiler supports the cxx flag.
  if(${${has_flag}})
    target_compile_options(${target} INTERFACE "${flag}")
  elseif("${alternative_flag}")
    add_target_compile_flag_if_supported_ex(${target} ${alternative_flag} "")
  endif()
endfunction()

# ToDo: Is there a better way to private an optional third argument?
macro(add_target_compile_flag_if_supported target flag)
  add_target_compile_flag_if_supported_ex("${target}" "${flag}" "")
endmacro()

# Several standard warnings disabled for now so no code change is required as
# part of CMake-Switch commit.
set(CLANG_GCC_WARNINGS
    -Wall
    -Wextra
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wpedantic
    # To be enabled in the future:
    #
    # * -Wshadow
    # * -Wold-style-cast
    # * -Wconversion
    # * -Wsign-conversion
    # * -Wnull-dereference
    # * -Wformat=2
)
# Tested separately as this is not supported by clang 3.4
add_target_compile_flag_if_supported(standard_warnings "-Wdouble-promotion")

if(WARNINGS_AS_ERRORS)
  set(CLANG_GCC_WARNINGS ${CLANG_GCC_WARNINGS} -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Exact version or reason unknown, discovered in Ubuntu 14 docker test with clang 3.4
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
    # reserved-id-macro issued by clang 3.6 - 3.9 for libb2 blake2.h
    set(CLANG_GCC_WARNINGS ${CLANG_GCC_WARNINGS}  -Qunused-arguments -Wno-error=unreachable-code -Wno-error=reserved-id-macro)
  endif()

  target_compile_options(
    standard_warnings
    INTERFACE ${CLANG_GCC_WARNINGS}
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

  # If compiler supports shadow-field-in-constructor, disable only that.
  # Otherwise disable shadow.
  add_target_compile_flag_if_supported_ex(
    standard_warnings "-Wno-shadow-field-in-constructor" "-Wno-shadow")

  # Disable C++20 compatibility for now.
  add_target_compile_flag_if_supported(standard_warnings "-Wno-c++2a-compat")

  # If compiler supports these warnings, they have to be disabled for now.
  add_target_compile_flag_if_supported(standard_warnings
                                       "-Wno-zero-as-null-pointer-constant")
  add_target_compile_flag_if_supported(standard_warnings
                                       "-Wno-undefined-func-template")
  add_target_compile_flag_if_supported(standard_warnings
                                       "-Wno-return-std-move-in-c++11")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(
    standard_warnings
    INTERFACE ${CLANG_GCC_WARNINGS}

    -Wlogical-op # warn about logical operations being used where bitwise were probably wanted.

    #
    # To be enabled in the future:
    #
    # * -Wmisleading- indentation # warn if indentation implies blocks where
    #   blocks do not exist
    # * -Wduplicated-cond # warn if if / else chain has duplicated conditions
    # * -Wduplicated-branches # warn if if / else branches have duplicated code
    # * -Wuseless-cast # warn if you perform a cast to the same type
  )

  # Exact version or reason unknown, discovered in Ubuntu 14 docker test with gcc 4.8.4
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.5)
    add_target_compile_flag_if_supported(standard_warnings "-Wno-missing-field-initializers")
    add_target_compile_flag_if_supported(standard_warnings "-Wno-unused-variable")
  endif()
endif()
