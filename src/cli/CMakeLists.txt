add_executable(doccraft main.cc)
target_link_libraries(doccraft PRIVATE doccraft_cfb doccraft_doc doccraft_model doccraft_docx)
target_compile_features(doccraft PUBLIC cxx_std_23)
add_test(NAME inspect_plain_text COMMAND doccraft inspect
         ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc)
set_tests_properties(inspect_plain_text PROPERTIES PASS_REGULAR_EXPRESSION "WordDocument")
