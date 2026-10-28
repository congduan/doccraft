add_library(doccraft_ooxml STATIC
  validator.cc
  generated/wml_schema_data.cc)
target_include_directories(doccraft_ooxml PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
# The validator only reuses the vendored XML parser; it must not depend on
# doccraft_docx or doccraft_model, and nothing in the shipped binary links it.
target_link_libraries(doccraft_ooxml PUBLIC doccraft_pugixml)
target_compile_features(doccraft_ooxml PUBLIC cxx_std_23)
