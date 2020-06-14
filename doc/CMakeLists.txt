
find_program (ASCIIDOC_EXE asciidoc)
mark_as_advanced(ASCIIDOC_EXE) # don't show in ccmake
if (NOT ASCIIDOC_EXE)
    message (WARNING "Could not find asciidoc: documentation & manpage will not be generated")
else()
  #
  # documentation
  #
  add_custom_target(documentation)

  function(generate_html in out)
    add_custom_command (
      TARGET documentation
      COMMAND ${ASCIIDOC_EXE} -o "${CMAKE_BINARY_DIR}/${out}" -a revnumber="${VERSION}" -a toc -b xhtml11 "${CMAKE_SOURCE_DIR}/${in}"
      BYPRODUCTS "${out}"
    )
  endfunction()

  add_custom_command (
    TARGET documentation
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/doc"
    COMMAND misc/update_authors.sh
    BYPRODUCTS "doc"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  generate_html(LICENSE.adoc LICENSE.html)
  generate_html(doc/AUTHORS.adoc doc/AUTHORS.html)
  generate_html(doc/MANUAL.adoc doc/MANUAL.html)
  generate_html(doc/NEWS.adoc doc/NEWS.html)

  #
  # manpage
  #

  find_program (A2X_EXE a2x)
  mark_as_advanced(A2X_EXE) # don't show in ccmake
  if (NOT A2X_EXE)
      message (WARNING "Could not find a2x: manpage will not be generated")
  else()
    add_custom_target(manpage)

    # MANUAL.adoc -> MANUAL.xml -> MANUAL manpage
    add_custom_command (
            TARGET manpage
            COMMAND ${ASCIIDOC_EXE} -o - -a revnumber=${VERSION} -d manpage -b docbook "${CMAKE_SOURCE_DIR}/doc/MANUAL.adoc" | perl -pe 's!<literal>\(.*?\)</literal>!<emphasis role="strong">\\1</emphasis>!g' > "${CMAKE_BINARY_DIR}/MANUAL.xml"
            BYPRODUCTS "${CMAKE_BINARY_DIR}/MANUAL.xml"
        )

        add_custom_command (
            TARGET manpage
            COMMAND a2x --doctype manpage --format manpage "${CMAKE_BINARY_DIR}/MANUAL.xml"
            BYPRODUCTS ccache.1
        )
  endif()
endif()
