add_library(blake3 STATIC blake3.c blake3_dispatch.c blake3_portable.c)

target_link_libraries(blake3 PRIVATE standard_settings)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(blake_source_type asm)
  set(blake_suffix "_x86-64_unix.S")
else()
  set(blake_source_type c)
  set(blake_suffix ".c")
endif()

include(CheckAsmCompilerFlag)
include(CheckCCompilerFlag)

function(add_source_if_enabled feature compile_flags)
  string(TOUPPER "have_${blake_source_type}_${feature}" have_feature)
  if(${blake_source_type} STREQUAL "asm")
    check_asm_compiler_flag(${compile_flags} ${have_feature})
  else()
    check_c_compiler_flag(${compile_flags} ${have_feature})
  endif()

  if(${have_feature})
    target_sources(blake3 PRIVATE blake3_${feature}${blake_suffix})
    set_property(
      SOURCE blake3_${feature}${blake_suffix}
      APPEND PROPERTY COMPILE_FLAGS ${compile_flags})
  else()
    string(TOUPPER "blake3_no_${feature}" no_feature)
    target_compile_definitions(blake3 PRIVATE ${no_feature})
  endif()
endfunction()

add_source_if_enabled(sse41 "-msse4.1")
add_source_if_enabled(avx2 "-mavx2")
add_source_if_enabled(avx512 "-mavx512f -mavx512vl")

# TODO: how to detect ARM NEON support?
# If NEON, define BLAKE3_USE_NEON and build blake3_neon.c
