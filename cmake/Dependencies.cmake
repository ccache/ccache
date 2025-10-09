include(FindPackageHandleStandardArgs)

if(POLICY CMP0135)
  # Set timestamps on extracted files to time of extraction.
  cmake_policy(SET CMP0135 NEW)
endif()

# Hint about usage of the previously supported OFFLINE variable.
if(OFFLINE)
  message(FATAL_ERROR "Please use -D DEPS=LOCAL instead of -D OFFLINE=ON")
endif()

# How to locate/retrieve dependencies. See the Dependencies section in
# doc/install.md.
set(DEPS AUTO CACHE STRING "How to retrieve third party dependencies")
set_property(CACHE DEPS PROPERTY STRINGS AUTO DOWNLOAD LOCAL)

if(FETCHCONTENT_FULLY_DISCONNECTED)
  message(STATUS "Setting DEPS=LOCAL as FETCHCONTENT_FULLY_DISCONNECTED is set")
  set(DEPS LOCAL)
endif()

find_package(Blake3 1.4.0 MODULE REQUIRED)
if(HTTP_STORAGE_BACKEND)
  find_package(CppHttplib 0.10.6 MODULE REQUIRED)
endif()
find_package(Fmt 8.0.0 MODULE REQUIRED)
find_package(NonstdSpan 0.10.3 MODULE REQUIRED)
find_package(TlExpected 1.1.0 MODULE REQUIRED)
find_package(Xxhash 0.8.0 MODULE REQUIRED)
find_package(Zstd 1.3.4 MODULE REQUIRED)

if(ENABLE_TESTING)
  find_package(Doctest 2.4.6 MODULE REQUIRED)
endif()

if(REDIS_STORAGE_BACKEND)
  find_package(Hiredis 0.13.3 MODULE REQUIRED)
endif()
