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

macro(add_compile_flag_if_supported_ex varname flag alternative_flag)
  # has_flag will contain "HAS_$flag" so each flag gets a unique HAS variable.
  generate_unique_has_flag_var_name("${flag}" "has_flag")

  # Instead of passing "has_flag" this passes the content of has_flag.
  check_cxx_compiler_flag("${flag}" "${has_flag}")

  if(${${has_flag}})
    list(APPEND "${varname}" "${flag}")
  elseif("${alternative_flag}")
    add_compile_flag_if_supported_ex("${varname}" ${alternative_flag} "")
  endif()
endmacro()

macro(add_compile_flag_if_supported varname flag)
  add_compile_flag_if_supported_ex("${varname}" "${flag}" "")
endmacro()

set(
  _clang_gcc_warnings
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
add_compile_flag_if_supported(_clang_gcc_warnings "-Wdouble-promotion")

if(WARNINGS_AS_ERRORS)
  list(APPEND _clang_gcc_warnings -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(APPEND CCACHE_COMPILER_WARNINGS ${_clang_gcc_warnings})

  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
    list(
      APPEND
      CCACHE_COMPILER_WARNINGS
      -Qunused-arguments
      -Wno-error=unreachable-code
    )
  endif()

  list(
    APPEND
    CCACHE_COMPILER_WARNINGS
    -Weverything
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-constexpr-not-const
    -Wno-conversion
    -Wno-disabled-macro-expansion
    -Wno-documentation-unknown-command
    -Wno-exit-time-destructors
    -Wno-format-nonliteral
    -Wno-global-constructors
    -Wno-implicit-fallthrough
    -Wno-old-style-cast
    -Wno-padded
    -Wno-shadow # Warnings in fmtlib
    -Wno-shorten-64-to-32
    -Wno-sign-conversion
    -Wno-signed-enum-bitfield # Warnings in fmtlib
    -Wno-weak-vtables
  )

  # If compiler supports -Wshadow-field-in-constructor, disable only that.
  # Otherwise disable shadow.
  add_compile_flag_if_supported_ex(
    CCACHE_COMPILER_WARNINGS "-Wno-shadow-field-in-constructor" "-Wno-shadow")

  # Disable C++20 compatibility for now.
  add_compile_flag_if_supported(CCACHE_COMPILER_WARNINGS "-Wno-c++2a-compat")

  # If compiler supports these warnings they have to be disabled for now.
  add_compile_flag_if_supported(
    CCACHE_COMPILER_WARNINGS "-Wno-zero-as-null-pointer-constant")
  add_compile_flag_if_supported(
    CCACHE_COMPILER_WARNINGS "-Wno-undefined-func-template")
  add_compile_flag_if_supported(
    CCACHE_COMPILER_WARNINGS "-Wno-return-std-move-in-c++11")

  # -Wthread-safety-analysis (on FreeBSD when dev mode is enabled) gives false
  # positive results currently. See PR #730.
  if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    add_compile_flag_if_supported(
      CCACHE_COMPILER_WARNINGS "-Wno-thread-safety-analysis")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(
    APPEND
    CCACHE_COMPILER_WARNINGS
    ${_clang_gcc_warnings}

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
    add_compile_flag_if_supported(
      CCACHE_COMPILER_WARNINGS "-Wno-missing-field-initializers")
    add_compile_flag_if_supported(
      CCACHE_COMPILER_WARNINGS "-Wno-unused-variable")
  endif()
elseif(MSVC)
  # Remove any warning level flags added by CMake.
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}")
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

  if(WARNINGS_AS_ERRORS)
    list(APPEND CCACHE_COMPILER_WARNINGS /WE)
  endif()

  list(
    APPEND
    CCACHE_COMPILER_WARNINGS
    /W4
    # Ignore bad macro in winbase.h triggered by /Zc:preprocessor:
    /wd5105
    # Conversion warnings:
    /wd4244
    /wd4267
    # Assignment in conditional:
    /wd4706
    # Non-underscore-prefixed POSIX functions:
    /wd4996
  )
endif()
