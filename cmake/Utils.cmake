include(CheckCXXCompilerFlag)

# check_cxx_compiler_flag caches the result, so a unique variable name is
# required for every flag to be checked.
#
# Parameters:
#
# * flag [in], e.g. FLAG
# * var_name_of_var_name [in], e.g. "TEMP". This is the variable that "HAS_FLAG"
#   will be written to.
function(_generate_unique_has_flag_var_name flag var_name_of_var_name)
  string(REGEX REPLACE "[=-]" "_" var_name "${flag}")
  string(TOUPPER "${var_name}" var_name)
  set(${var_name_of_var_name} "HAS_${var_name}" PARENT_SCOPE)
endfunction()

macro(add_compile_flag_if_supported_ex varname flag alternative_flag)
  # has_flag will contain "HAS_$flag" so each flag gets a unique HAS variable.
  _generate_unique_has_flag_var_name("${flag}" "has_flag")

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

set(dependencies "" CACHE INTERNAL "")

function(register_dependency name origin version)
  list(APPEND dependencies "${name}:${origin}:${version}")
  set(dependencies "${dependencies}" CACHE INTERNAL "")
endfunction()

function(print_dependency_summary prefix)
  list(SORT dependencies)

  list(LENGTH dependencies n_deps)
  math(EXPR n_deps_minus_1 "${n_deps} - 1")

  set(max_name_length 0)
  set(max_version_length 0)
  foreach(entry IN LISTS dependencies)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 2 version)

    string(LENGTH "${name}" name_length)
    if("${name_length}" GREATER "${max_name_length}")
      set(max_name_length "${name_length}")
    endif()

    string(LENGTH "${version}" version_length)
    if("${version_length}" GREATER "${max_version_length}")
      set(max_version_length "${version_length}")
    endif()
  endforeach()

  foreach(entry IN LISTS dependencies)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 origin)
    list(GET parts 2 version)

    string(LENGTH "${name}" name_length)
    math(EXPR pad_count "${max_name_length} - ${name_length}")
    string(REPEAT " " "${pad_count}" name_pad)

    string(LENGTH "${version}" version_length)
    math(EXPR pad_count "${max_version_length} - ${version_length}")
    string(REPEAT " " "${pad_count}" version_pad)

    message(STATUS "${prefix}${name}${name_pad} ${version}${version_pad} ${origin}")
  endforeach()
endfunction()

function(add_header_only_library lib_name)
  cmake_parse_arguments(arg "" "DIR;URL;SHA256;SUBDIR" "" ${ARGN})

  if(arg_DIR)
    set(_src_dir "${arg_DIR}")
  else()
    if(NOT arg_URL)
    message(FATAL_ERROR "Missing required argument: URL or DIR")
    endif()
    if(NOT arg_SHA256)
      message(FATAL_ERROR "Missing required argument: SHA256")
    endif()
    if(NOT arg_SUBDIR)
      set(arg_SUBDIR .)
    endif()

    set(_src_dir "${CMAKE_BINARY_DIR}/_deps/${lib_name}-src")
    get_filename_component(_header "${arg_URL}" NAME)
    file(
      DOWNLOAD "${arg_URL}" "${_src_dir}/${arg_SUBDIR}/${_header}"
      EXPECTED_HASH "SHA256=${arg_SHA256}"
      STATUS _download_status
    )
    if(NOT "${_download_status}" EQUAL 0)
      message(FATAL_ERROR "Failed to download ${arg_URL}: ${_download_status}")
    endif()
  endif()

  add_library("dep_${lib_name}" INTERFACE)
  target_include_directories("dep_${lib_name}" SYSTEM INTERFACE "${_src_dir}")
endfunction()
