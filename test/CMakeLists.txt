function(addtest name)
  add_test(
    NAME "test.${name}"
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/run ${name}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_tests_properties(
    "test.${name}"
    PROPERTIES
    ENVIRONMENT "CCACHE=$<TARGET_FILE:ccache>;EXIT_IF_SKIPPED=true"
    SKIP_RETURN_CODE 125)
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
addtest(basedir)
addtest(cache_levels)
addtest(cleanup)
addtest(color_diagnostics)
addtest(cpp1)
addtest(debug_prefix_map)
addtest(depend)
addtest(direct)
addtest(direct_gcc)
addtest(fileclone)
addtest(hardlink)
addtest(inode_cache)
addtest(input_charset)
addtest(ivfsoverlay)
addtest(masquerading)
addtest(modules)
addtest(multi_arch)
addtest(no_compression)
addtest(nocpp2)
addtest(nvcc)
addtest(nvcc_direct)
addtest(nvcc_ldir)
addtest(nvcc_nocpp2)
addtest(pch)
addtest(profiling)
addtest(profiling_clang)
addtest(profiling_gcc)
addtest(profiling_hip_clang)
addtest(readonly)
addtest(readonly_direct)
addtest(sanitize_blacklist)
addtest(secondary_file)
addtest(secondary_http)
addtest(secondary_redis)
addtest(secondary_url)
addtest(serialize_diagnostics)
addtest(source_date_epoch)
addtest(split_dwarf)
addtest(stats_log)
addtest(upgrade)
