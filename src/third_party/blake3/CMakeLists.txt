add_library(blake3 STATIC blake3.c blake3_dispatch_ccache.c blake3_portable.c)

target_link_libraries(blake3 PRIVATE standard_settings)

include(CheckCSourceCompiles)

function(add_source_if_enabled feature compile_flags intrinsic)
  # First check if it's possible to use the assembler variant for the feature.
  string(TOUPPER "have_asm_${feature}" have_feature)
  if(NOT DEFINED "${have_feature}" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(WIN32)
      set(suffix "_x86-64_windows_gnu.S")
    else()
      set(suffix "_x86-64_unix.S")
    endif()

    if(NOT CMAKE_REQUIRED_QUIET)
      message(STATUS "Performing Test ${have_feature}")
    endif()

    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(
      ${have_feature}
      ${CMAKE_CURRENT_BINARY_DIR}
      "${CMAKE_CURRENT_SOURCE_DIR}/blake3_${feature}${suffix}"
      COMPILE_DEFINITIONS ${compile_flags})
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)

    if(NOT CMAKE_REQUIRED_QUIET)
      if (${${have_feature}})
        message(STATUS "Performing Test ${have_feature} - Success")
      else()
        message(STATUS "Performing Test ${have_feature} - Failed")
      endif()
    endif()
  endif()

  # If the assembler variant didn't work, try the c variant.
  if(NOT ${have_feature})
    string(TOUPPER "have_c_${feature}" have_feature)
    set(suffix ".c")

    set(CMAKE_REQUIRED_FLAGS ${compile_flags})
    check_c_source_compiles(
      [=[
        #include <immintrin.h>
        int main() { ${intrinsic}; return 0; }
      ]=]
      ${have_feature})
    unset(CMAKE_REQUIRED_FLAGS)
  endif()

  if(${have_feature})
    target_sources(blake3 PRIVATE blake3_${feature}${suffix})
    set_property(
      SOURCE blake3_${feature}${suffix}
      APPEND PROPERTY COMPILE_FLAGS ${compile_flags})
  else()
    string(TOUPPER "blake3_no_${feature}" no_feature)
    target_compile_definitions(blake3 PRIVATE ${no_feature})
  endif()
endfunction()

# https://software.intel.com/sites/landingpage/IntrinsicsGuide/
add_source_if_enabled(sse2 "-msse2" "_mm_set1_epi32(42)")
add_source_if_enabled(sse41 "-msse4.1" "_mm_test_all_ones(_mm_set1_epi32(42))")
add_source_if_enabled(avx2 "-mavx2" "_mm256_abs_epi8(_mm256_set1_epi32(42))")
add_source_if_enabled(avx512 "-mavx512f -mavx512vl" "_mm256_abs_epi64(_mm256_set1_epi32(42))")

# Neon is always available on AArch64
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  # https://developer.arm.com/architectures/instruction-sets/simd-isas/neon/intrinsics
  check_c_source_compiles(
    [=[
      #include <arm_neon.h>
      int main() { vdupq_n_s32(42); return 0; }
    ]=]
    HAVE_NEON)
  if(HAVE_NEON)
    target_sources(blake3 PRIVATE blake3_neon.c)
    target_compile_definitions(blake3 PRIVATE BLAKE3_USE_NEON)
  endif()
endif()
