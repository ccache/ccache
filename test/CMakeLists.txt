function(addtest name)
  add_test(
    NAME "test.${name}"
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/run ${name}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_tests_properties(
    "test.${name}"
    PROPERTIES
    ENVIRONMENT "CCACHE=${CMAKE_BINARY_DIR}/ccache;EXIT_IF_SKIPPED=true")

  if(${CMAKE_VERSION} VERSION_LESS "3.9")
    # Older CMake versions treat skipped tests as errors. Therefore, resort to
    # parsing output for those cases (exit code is not considered). Skipped
    # tests will appear as "Passed".
    set_tests_properties(
      "test.${name}"
      PROPERTIES
      PASS_REGULAR_EXPRESSION "PASSED|Passed|Skipped"
      FAIL_REGULAR_EXPRESSION "[Ww]arning|[Ff]ail|[Er]rror")
  else()
    set_tests_properties("test.${name}" PROPERTIES SKIP_RETURN_CODE 125)
  endif()

endfunction()

if(${CMAKE_VERSION} VERSION_LESS "3.15")
  set(clean_files_prop_name ADDITIONAL_MAKE_CLEAN_FILES)
else()
  set(clean_files_prop_name ADDITIONAL_CLEAN_FILES)
endif()
set_property(
  DIRECTORY PROPERTY
  ${clean_files_prop_name} "${CMAKE_BINARY_DIR}/testdir")

addtest(base)
addtest(nocpp2)
addtest(cpp1)
addtest(multi_arch)
addtest(serialize_diagnostics)
addtest(color_diagnostics)
addtest(sanitize_blacklist)
addtest(debug_prefix_map)
addtest(profiling)
addtest(profiling_gcc)
addtest(profiling_clang)
addtest(profiling_hip_clang)
addtest(split_dwarf)
addtest(masquerading)
addtest(hardlink)
addtest(fileclone)
addtest(direct)
addtest(direct_gcc)
addtest(depend)
addtest(basedir)
addtest(no_compression)
addtest(readonly)
addtest(readonly_direct)
addtest(cleanup)
addtest(pch)
addtest(modules)
addtest(upgrade)
addtest(input_charset)
addtest(nvcc)
addtest(nvcc_direct)
addtest(nvcc_ldir)
addtest(nvcc_nocpp2)
addtest(inode_cache)
