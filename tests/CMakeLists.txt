add_executable(cfb_reader_test cfb_reader_test.cc cfb_test_util.cc cfb_test_util.h)
target_link_libraries(cfb_reader_test PRIVATE doccraft_cfb gtest_main)
target_compile_features(cfb_reader_test PUBLIC cxx_std_23)
target_compile_definitions(cfb_reader_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(GoogleTest)
gtest_discover_tests(cfb_reader_test)
add_executable(m2_test m2_test.cc cfb_test_util.cc)
target_link_libraries(m2_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m2_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME docx_zip_validate COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/docx/validate_zip.py
  $<TARGET_FILE:doccraft> ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc)
add_test(NAME corpus_text_consistency COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/corpus/compare_text.py $<TARGET_FILE:doccraft>)
