add_subdirectory(base)
add_subdirectory(crypto)
add_subdirectory(cfb)
add_subdirectory(doc)
add_subdirectory(model)
add_subdirectory(docx)
# Test-only schema validator; it reuses doccraft_pugixml from docx and is
# never linked into the CLI binary.
add_subdirectory(ooxml)
add_subdirectory(cli)
