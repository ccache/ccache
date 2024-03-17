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
