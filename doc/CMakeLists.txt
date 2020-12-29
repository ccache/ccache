find_program(ASCIIDOC_EXE asciidoc)
mark_as_advanced(ASCIIDOC_EXE) # Don't show in CMake UIs

if(NOT ASCIIDOC_EXE)
  message(NOTICE "Could not find asciidoc; documentation will not be generated")
else()
  #
  # HTML documentation
  #
  function(generate_html adoc_file)
    get_filename_component(base_name "${adoc_file}" NAME_WE)
    set(html_file "${base_name}.html")
    add_custom_command(
      OUTPUT "${html_file}"
      COMMAND
        ${ASCIIDOC_EXE}
          -o "${html_file}"
          -a revnumber="${CCACHE_VERSION}"
          -a toc
          -b xhtml11
          "${CMAKE_SOURCE_DIR}/${adoc_file}"
      MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/${adoc_file}"
    )
    set(html_files "${html_files}" "${html_file}" PARENT_SCOPE)
  endfunction()

  generate_html(LICENSE.adoc)
  generate_html(doc/AUTHORS.adoc)
  generate_html(doc/MANUAL.adoc)
  generate_html(doc/NEWS.adoc)

  add_custom_target(doc-html DEPENDS "${html_files}")
  set(doc_files "${html_files}")

  #
  # Man page
  #
  find_program(A2X_EXE a2x)
  mark_as_advanced(A2X_EXE) # Don't show in CMake UIs
  if(NOT A2X_EXE)
    message(WARNING "Could not find a2x; man page will not be generated")
  else()
    # MANUAL.adoc -> MANUAL.xml -> man page
    add_custom_command(
      OUTPUT MANUAL.xml
      COMMAND
        ${ASCIIDOC_EXE}
          -o -
          -a revnumber=${CCACHE_VERSION}
          -d manpage
          -b docbook "${CMAKE_SOURCE_DIR}/doc/MANUAL.adoc"
        | perl -pe 's!<literal>\(.*?\)</literal>!<emphasis role="strong">\\1</emphasis>!g'
            >MANUAL.xml
      MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/doc/MANUAL.adoc"
    )
    add_custom_command(
      OUTPUT ccache.1
      COMMAND ${A2X_EXE} --doctype manpage --format manpage MANUAL.xml
      MAIN_DEPENDENCY MANUAL.xml
    )
    add_custom_target(doc-man-page DEPENDS ccache.1)
    install(
      FILES "${CMAKE_CURRENT_BINARY_DIR}/ccache.1"
      DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    )
    set(doc_files "${doc_files}" ccache.1)
  endif()

  add_custom_target(doc ALL DEPENDS "${doc_files}")
endif()
