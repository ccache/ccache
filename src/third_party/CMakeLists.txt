add_library(third_party_lib STATIC format.cpp getopt_long.c xxhash.c)
if(ENABLE_TRACING)
  target_sources(third_party_lib PRIVATE minitrace.c)
endif()

# Treat third party headers as system files. (No warning from those headers)
target_include_directories(third_party_lib PRIVATE ${CMAKE_BINARY_DIR} . SYSTEM
                           INTERFACE .)

target_link_libraries(third_party_lib PRIVATE standard_settings)

# third_party (for now) has way fewer compiler warnings enabled than the rest of
# the code.

# These warnings are enabled by default even without e.g. -Wall. But (for now)
# we don't want even them in third_party.
target_compile_options(
  third_party_lib
  PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration
          -Wno-int-conversion>)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(third_party_lib
                         PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-attributes>)
endif()

# The headers are included from the rest of the project, so turn off warnings as
# requried (for now).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(third_party_lib INTERFACE -Wno-shadow)
endif()
