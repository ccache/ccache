mark_as_advanced(FMT_INCLUDE_DIR FMT_LIBRARY)

if(DEP_FMT STREQUAL "BUNDLED")
  message(STATUS "Using bundled Fmt as requested")
else()
  find_path(FMT_INCLUDE_DIR fmt/core.h)
  find_library(FMT_LIBRARY fmt)
  if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
    file(READ "${FMT_INCLUDE_DIR}/fmt/core.h" _fmt_core_h)
    string(REGEX MATCH "#define FMT_VERSION ([0-9]+)" _ "${_fmt_core_h}")
    math(EXPR _fmt_major "${CMAKE_MATCH_1} / 10000")
    math(EXPR _fmt_minor "${CMAKE_MATCH_1} / 100 % 100")
    math(EXPR _fmt_patch "${CMAKE_MATCH_1} % 100")
    set(_fmt_version_string "${_fmt_major}.${_fmt_minor}.${_fmt_patch}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_fmt_version_string}" VERSION_GREATER_EQUAL "${Fmt_FIND_VERSION}")
      message(STATUS "Using system Fmt (${FMT_LIBRARY})")
      add_library(dep_fmt UNKNOWN IMPORTED)
      set_target_properties(
        dep_fmt
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FMT_INCLUDE_DIR}"
        IMPORTED_LOCATION "${FMT_LIBRARY}"
      )
      register_dependency(Fmt "SYSTEM (${FMT_LIBRARY})" "${_fmt_version_string}")
    endif()
  endif()
  if(NOT TARGET dep_fmt)
    message(STATUS "Using bundled Fmt since Fmt>=${Fmt_FIND_VERSION} was not found locally")
  endif()
endif()
