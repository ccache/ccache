find_program(ASCIIDOCTOR_EXE asciidoctor)
mark_as_advanced(ASCIIDOCTOR_EXE) # Don't show in CMake UIs

if(NOT ASCIIDOCTOR_EXE)
  message(WARNING "Could not find asciidoctor; documentation will not be generated")
else()
  function(generate_doc backend adoc_file output_file)
    add_custom_command(
      OUTPUT "${output_file}"
      COMMAND
        ${ASCIIDOCTOR_EXE}
          -o "${output_file}"
          -a revnumber="${CCACHE_VERSION}"
          -a icons=font
          -a toc=left
          -a sectanchors
          -a stylesheet="${CMAKE_CURRENT_SOURCE_DIR}/ccache-doc.css"
          -b "${backend}"
          "${CMAKE_SOURCE_DIR}/${adoc_file}"
      MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/${adoc_file}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ccache-doc.css"
    )
    if(${backend} STREQUAL manpage)
      # Convert monospace to bold since that's typically rendered better when
      # viewing the man page.
      add_custom_command(
        OUTPUT "${output_file}"
        COMMAND perl -pi -e "'s!\\\\f\\(CR(.*?)\\\\fP!\\\\fB\\1\\\\fP!g'" "${output_file}"
        APPEND
      )
    endif()
    if("${backend}" STREQUAL manpage)
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${output_file}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
      )
    else()
      set(html_doc_files "${html_doc_files}" "${output_file}" PARENT_SCOPE)
      install(
        FILES "${CMAKE_SOURCE_DIR}/${adoc_file}" "${CMAKE_CURRENT_BINARY_DIR}/${output_file}"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
      )
    endif()
  endfunction()

  #
  # HTML documentation
  #
  generate_doc(html    LICENSE.adoc     LICENSE.html)
  generate_doc(html    doc/AUTHORS.adoc AUTHORS.html)
  generate_doc(html    doc/MANUAL.adoc  MANUAL.html)
  generate_doc(html    doc/NEWS.adoc    NEWS.html)
  add_custom_target(doc-html DEPENDS "${html_doc_files}")

  #
  # Man page
  #
  generate_doc(manpage doc/MANUAL.adoc ccache.1)
  add_custom_target(doc-man-page DEPENDS ccache.1)

  add_custom_target(doc ALL DEPENDS doc-html doc-man-page)
endif()
