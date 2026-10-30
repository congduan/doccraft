add_subdirectory(base)
add_subdirectory(crypto)
add_subdirectory(cfb)
add_subdirectory(doc)
add_subdirectory(model)
add_subdirectory(docx)
add_subdirectory(api)

if(DOCCRAFT_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(DOCCRAFT_BUILD_TESTS)
  # Test-only schema validator; it reuses doccraft_pugixml from docx and is
  # never linked into the CLI binary.
  add_subdirectory(ooxml)
endif()
