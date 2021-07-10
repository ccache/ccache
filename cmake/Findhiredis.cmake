find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(HIREDIS REQUIRED hiredis>=${hiredis_FIND_VERSION})
  find_library(HIREDIS_LIBRARY ${HIREDIS_LIBRARIES} HINTS ${HIREDIS_LIBDIR})
  find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ${HIREDIS_PREFIX}/include)
else()
  find_library(HIREDIS_LIBRARY hiredis)
  find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  hiredis
  "please install libhiredis or disable with -DREDIS_STORAGE_BACKEND=OFF"
  HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)
mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

add_library(HIREDIS::HIREDIS UNKNOWN IMPORTED)
set_target_properties(
  HIREDIS::HIREDIS
  PROPERTIES
  IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
  INTERFACE_COMPILE_OPTIONS "${HIREDIS_CFLAGS_OTHER}"
  INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}")

include(FeatureSummary)
set_package_properties(
  hiredis
  PROPERTIES
  URL "https://github.com/redis/hiredis"
  DESCRIPTION "Hiredis is a minimalistic C client library for the Redis database")
