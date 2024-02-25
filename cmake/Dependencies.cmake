if(POLICY CMP0135)
  # Set timestamps on extracted files to time of extraction.
  cmake_policy(SET CMP0135 NEW)
endif()

set(ZSTD_FROM_INTERNET AUTO CACHE STRING "Download and use libzstd from the Internet")
set_property(CACHE ZSTD_FROM_INTERNET PROPERTY STRINGS AUTO ON OFF)

set(HIREDIS_FROM_INTERNET AUTO CACHE STRING "Download and use libhiredis from the Internet")
set_property(CACHE HIREDIS_FROM_INTERNET PROPERTY STRINGS AUTO ON OFF)

option(
  OFFLINE "Do not download anything from the internet"
  "$(FETCHCONTENT_FULLY_DISCONNECTED}"
)
if(OFFLINE)
  set(FETCHCONTENT_FULLY_DISCONNECTED ON)
  set(ZSTD_FROM_INTERNET OFF)
  set(HIREDIS_FROM_INTERNET OFF)
endif()

find_package(zstd 1.1.2 MODULE REQUIRED)

if(REDIS_STORAGE_BACKEND)
  find_package(hiredis 0.13.3 MODULE REQUIRED)
endif()
