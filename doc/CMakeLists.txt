find_program(ASCIIDOCTOR_EXE asciidoctor)
mark_as_advanced(ASCIIDOCTOR_EXE) # Don't show in CMake UIs

find_program(PANDOC_EXE pandoc)
mark_as_advanced(PANDOC_EXE) # Don't show in CMake UIs

if(NOT ASCIIDOCTOR_EXE)
  message(WARNING "Could not find asciidoctor; documentation will not be generated")
else()
  function(generate_doc type adoc_file output_file)
    add_custom_command(
      OUTPUT "${output_file}"
      COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-${type}"
        "${ASCIIDOCTOR_EXE}"
        "${PANDOC_EXE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/ccache-doc.css"
        "${CCACHE_VERSION}"
        "${CMAKE_SOURCE_DIR}/${adoc_file}"
        "${output_file}"
      MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/${adoc_file}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ccache-doc.css"
    )

    if("${type}" STREQUAL manpage)
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${output_file}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
      )
    elseif("${type}" STREQUAL html)
      set(html_doc_files "${html_doc_files}" "${output_file}" PARENT_SCOPE)
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${output_file}"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
      )
    elseif("${type}" STREQUAL markdown)
      set(markdown_doc_files "${markdown_doc_files}" "${output_file}" PARENT_SCOPE)
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${output_file}"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
      )
    endif()
  endfunction()

  #
  # HTML documentation
  #
  generate_doc(html    LICENSE.adoc     LICENSE.html)
  generate_doc(html    doc/authors.adoc AUTHORS.html)
  generate_doc(html    doc/manual.adoc  MANUAL.html)
  generate_doc(html    doc/news.adoc    NEWS.html)
  add_custom_target(doc-html DEPENDS "${html_doc_files}")

  if(PANDOC_EXE)
    #
    # Markdown documentation
    #
    generate_doc(markdown LICENSE.adoc     LICENSE.md)
    generate_doc(markdown doc/authors.adoc AUTHORS.md)
    generate_doc(markdown doc/manual.adoc  MANUAL.md)
    generate_doc(markdown doc/news.adoc    NEWS.md)
    add_custom_target(doc-markdown DEPENDS "${markdown_doc_files}")
  else()
    message(WARNING "Could not find pandoc; markdown documentation will not be generated")
  endif()

  #
  # Man page
  #
  generate_doc(manpage doc/manual.adoc ccache.1)
  add_custom_target(doc-man-page DEPENDS ccache.1)

  add_custom_target(doc ALL DEPENDS doc-html doc-man-page)
  if(PANDOC_EXE)
    add_dependencies(doc doc-markdown)
  endif()
endif()
