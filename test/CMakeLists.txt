function(addtest name)
  add_test(NAME "producttest.${name}"
    COMMAND ${CMAKE_SOURCE_DIR}/test/run ${name}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set(
    environment
    CCACHE=${CMAKE_BINARY_DIR}/ccache
    EXIT_IF_SKIPPED=true
  )
  set_tests_properties(
    "producttest.${name}"
    PROPERTIES
    ENVIRONMENT "${environment}"
    SKIP_RETURN_CODE 125)
endfunction()

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
