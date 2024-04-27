mark_as_advanced(FMT_INCLUDE_DIR FMT_LIBRARY)

if(DEPS STREQUAL "DOWNLOAD" OR DEP_FMT STREQUAL "DOWNLOAD")
  message(STATUS "Downloading Fmt as requested")
  set(_download_fmt TRUE)
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
      set(_fmt_origin "SYSTEM (${FMT_LIBRARY})")
      add_library(dep_fmt UNKNOWN IMPORTED)
      set_target_properties(
        dep_fmt
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FMT_INCLUDE_DIR}"
        IMPORTED_LOCATION "${FMT_LIBRARY}"
      )
    endif()
  endif()
  if(NOT _fmt_origin)
    if(DEPS STREQUAL "AUTO")
      message(STATUS "Downloading Fmt from the internet since Fmt>=${Fmt_FIND_VERSION} was not found locally and DEPS=AUTO")
      set(_download_fmt TRUE)
    else()
      message(FATAL_ERROR "Could not find Fmt>=${Fmt_FIND_VERSION}")
    endif()
  endif()
endif()

if(_download_fmt)
  set(_fmt_origin DOWNLOADED)
  set(_fmt_version_string 10.2.1)

  include(FetchContent)
  FetchContent_Declare(
    Fmt
    URL "https://github.com/fmtlib/fmt/releases/download/${_fmt_version_string}/fmt-${_fmt_version_string}.zip"
    URL_HASH SHA256=312151a2d13c8327f5c9c586ac6cf7cddc1658e8f53edae0ec56509c8fa516c9
  )
  FetchContent_MakeAvailable(Fmt)
  add_library(dep_fmt ALIAS fmt)
endif()

if("${_fmt_version_string}" VERSION_LESS 10.1.0)
  # https://github.com/fmtlib/fmt/issues/3415
  add_compile_flag_if_supported(CCACHE_COMPILER_WARNINGS "-Wno-dangling-reference")
endif()

register_dependency(Fmt "${_fmt_origin}" "${_fmt_version_string}")
