include_directories(${CMAKE_SOURCE_DIR})
include(GoogleTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Deterministic in-process mutation suite (M6): whole-pipeline seed mutation,
# directed sprm/FKP mutation and CLX/piece-table mutation. Zero crashes is the
# invariant; the gtest binary is the slowest test-safe way to run it in CI.
add_executable(fuzz_test fuzz_test.cc fuzz_util.cc)
target_link_libraries(fuzz_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx gtest_main)
target_compile_definitions(fuzz_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(fuzz_test)

# Optional continuous-fuzzing entry point. Requires clang with the libFuzzer
# runtime; not built by default and not part of ctest.
option(DOCCRAFT_ENABLE_LIBFUZZER
  "Build the libFuzzer driver for the convert pipeline (clang only)" OFF)
if(DOCCRAFT_ENABLE_LIBFUZZER)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "DOCCRAFT_ENABLE_LIBFUZZER requires the clang compiler")
  endif()
  add_executable(doccraft_fuzz fuzz_driver.cc fuzz_util.cc)
  target_link_libraries(doccraft_fuzz PRIVATE doccraft_cfb doccraft_doc
    doccraft_docx)
  target_compile_options(doccraft_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(doccraft_fuzz PRIVATE -fsanitize=fuzzer)
endif()

add_executable(crypto_test crypto_test.cc)
target_link_libraries(crypto_test PRIVATE doccraft_crypto gtest_main)
gtest_discover_tests(crypto_test)

add_executable(m6_rc4_test m6_rc4_test.cc)
target_link_libraries(m6_rc4_test PRIVATE doccraft_doc doccraft_crypto gtest_main)
gtest_discover_tests(m6_rc4_test)

add_executable(m6_fixture_gen m6_fixture_gen.cc cfb_test_util.cc)
target_link_libraries(m6_fixture_gen PRIVATE doccraft_cfb doccraft_crypto)

add_test(NAME m6_rc4_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m6_rc4_cli.py $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc
  ${CMAKE_SOURCE_DIR}/tests/fixtures/encrypted/plain-text-rc4-cryptoapi.doc
  ${CMAKE_SOURCE_DIR}/tests/fixtures/encrypted/plain-text-rc4-legacy.doc)

add_test(NAME m16_xor_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m16_xor_cli.py $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc
  ${CMAKE_SOURCE_DIR}/tests/fixtures/encrypted/plain-text-xor.doc)

add_executable(base_test base_test.cc)
target_link_libraries(base_test PRIVATE doccraft_base gtest_main)
target_compile_features(base_test PUBLIC cxx_std_23)
gtest_discover_tests(base_test)
add_executable(cfb_reader_test cfb_reader_test.cc cfb_test_util.cc cfb_test_util.h)
target_link_libraries(cfb_reader_test PRIVATE doccraft_cfb gtest_main)
target_compile_features(cfb_reader_test PUBLIC cxx_std_23)
target_compile_definitions(cfb_reader_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(cfb_reader_test)
add_executable(m2_test m2_test.cc cfb_test_util.cc)
target_link_libraries(m2_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m2_test)
add_executable(legacy_word_test legacy_word_test.cc)
target_link_libraries(legacy_word_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(legacy_word_test)
add_executable(legacy_styles_test legacy_styles_test.cc)
target_link_libraries(legacy_styles_test PRIVATE doccraft_doc gtest_main)
gtest_discover_tests(legacy_styles_test)
add_executable(m3_test m3_test.cc)
target_link_libraries(m3_test PRIVATE doccraft_cfb doccraft_doc gtest_main)
target_compile_definitions(m3_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_test)
add_executable(m3_batch2_test m3_batch2_test.cc)
target_link_libraries(m3_batch2_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_batch2_test)
add_executable(m3_batch3_test m3_batch3_test.cc)
target_link_libraries(m3_batch3_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m3_batch3_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_batch3_test)
add_test(NAME docx_zip_validate COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/docx/validate_zip.py
  $<TARGET_FILE:doccraft> ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc)
add_test(NAME corpus_text_consistency COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/corpus/compare_text.py $<TARGET_FILE:doccraft>)
add_test(NAME cli_input_path COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/cli_input_path.py $<TARGET_FILE:doccraft>)
add_executable(m15_bad_note_fixture m15_bad_note_fixture.cc cfb_test_util.cc)
target_link_libraries(m15_bad_note_fixture PRIVATE doccraft_cfb)
add_test(NAME m15_bad_note_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m15_bad_note_cli.py
  $<TARGET_FILE:m15_bad_note_fixture> $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/footnote-endnote.doc)
add_test(NAME m6_bad_textbox_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m6_bad_textbox_cli.py
  $<TARGET_FILE:m15_bad_note_fixture> $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/textbox.doc textbox)
add_test(NAME m6_bad_textbox_story_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m6_bad_textbox_cli.py
  $<TARGET_FILE:m15_bad_note_fixture> $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/textbox.doc textbox-story)

add_executable(ooxml_converter_test ooxml/converter_schema_test.cc)
target_link_libraries(ooxml_converter_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx doccraft_ooxml gtest_main)
target_compile_definitions(ooxml_converter_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(ooxml_converter_test)

add_executable(ooxml_validator_test ooxml/wml_validator_test.cc)
target_link_libraries(ooxml_validator_test PRIVATE doccraft_ooxml gtest_main)
gtest_discover_tests(ooxml_validator_test)

add_executable(m3_batch6_test m3_batch6_test.cc)
target_link_libraries(m3_batch6_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_batch6_test)
add_executable(m3_batch7_test m3_batch7_test.cc)
target_link_libraries(m3_batch7_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_batch7_test)
add_executable(m3_batch8_test m3_batch8_test.cc)
target_link_libraries(m3_batch8_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_batch8_test)
add_executable(m3_styles_test m3_styles_test.cc)
target_link_libraries(m3_styles_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_styles_test)
add_executable(charset_test charset_test.cc)
target_link_libraries(charset_test PRIVATE doccraft_base gtest_main)
target_compile_features(charset_test PUBLIC cxx_std_23)
gtest_discover_tests(charset_test)
add_executable(m3_fields_test m3_fields_test.cc)
target_link_libraries(m3_fields_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_fields_test)
add_executable(m8_story_fields_test m8_story_fields_test.cc)
target_link_libraries(m8_story_fields_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m8_story_fields_test)
add_executable(m3_runs_test m3_runs_test.cc)
target_link_libraries(m3_runs_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_runs_test)
add_executable(m3_nested_table_test m3_nested_table_test.cc)
target_link_libraries(m3_nested_table_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m3_nested_table_test)
add_executable(m3_headers_test m3_headers_test.cc)
target_link_libraries(m3_headers_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m3_headers_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_headers_test)
add_executable(m3_notes_test m3_notes_test.cc)
target_link_libraries(m3_notes_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m3_notes_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_notes_test)
add_executable(m3_comments_test m3_comments_test.cc)
target_link_libraries(m3_comments_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m3_comments_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_comments_test)
add_executable(m3_bookmarks_test m3_bookmarks_test.cc)
target_link_libraries(m3_bookmarks_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m3_bookmarks_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m3_bookmarks_test)

add_executable(m5_inline_images_test m5_inline_images_test.cc)
target_link_libraries(m5_inline_images_test PRIVATE doccraft_cfb doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m5_inline_images_test PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m5_inline_images_test)

add_executable(m4_textbox_test m4_textbox_test.cc)
target_link_libraries(m4_textbox_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx gtest_main)
target_compile_definitions(m4_textbox_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m4_textbox_test)

add_executable(docx_writer_test docx_writer_test.cc)
target_link_libraries(docx_writer_test PRIVATE doccraft_docx gtest_main)
gtest_discover_tests(docx_writer_test)
add_test(NAME docx_zip_order COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/docx/validate_order.py
  $<TARGET_FILE:doccraft> ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_executable(m6_textbox_degradation_test m6_textbox_degradation_test.cc)
target_link_libraries(m6_textbox_degradation_test PRIVATE doccraft_cfb
  doccraft_doc doccraft_docx gtest_main)
target_compile_definitions(m6_textbox_degradation_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m6_textbox_degradation_test)

add_executable(m14_textbox_test m14_textbox_test.cc)
target_link_libraries(m14_textbox_test PRIVATE doccraft_cfb doccraft_doc
                                                 doccraft_docx gtest_main)
target_compile_definitions(m14_textbox_test PRIVATE
                           DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m14_textbox_test)

add_executable(m19_columns_test m19_columns_test.cc)
target_link_libraries(m19_columns_test PRIVATE doccraft_doc doccraft_docx gtest_main)
gtest_discover_tests(m19_columns_test)
add_executable(m19_revisions_test m19_revisions_test.cc)
target_link_libraries(m19_revisions_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx gtest_main)
target_compile_definitions(m19_revisions_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m19_revisions_test)

add_executable(m4_ole_test m4_ole_test.cc cfb_test_util.cc)
target_link_libraries(m4_ole_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx gtest_main)
target_compile_definitions(m4_ole_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m4_ole_test)

add_executable(m4_fast_saved_test m4_fast_saved_test.cc)
target_link_libraries(m4_fast_saved_test PRIVATE doccraft_cfb doccraft_doc
  doccraft_docx gtest_main)
target_compile_definitions(m4_fast_saved_test PRIVATE
  DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(m4_fast_saved_test)

add_test(NAME degraded_fkp_warning COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/degraded_fkp.py $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/degraded-fkp.doc
  "warning: skipped CHPX FKP page" "After textbox")
add_test(NAME degraded_header_warning COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/degraded_fkp.py $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/degraded-header.doc
  "warning: skipped header stories" "Body line 0")
add_test(NAME degraded_sprm_warning COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/degraded_fkp.py $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/fixtures/degraded-sprm.doc
  "warning: skipped CHPX FKP page" "After textbox")

add_executable(m8_bad_papx_fixture m8_bad_papx_fixture.cc cfb_test_util.cc)
target_link_libraries(m8_bad_papx_fixture PRIVATE doccraft_cfb doccraft_doc)
add_test(NAME m8_bad_papx_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m8_bad_papx_cli.py
  $<TARGET_FILE:m8_bad_papx_fixture> $<TARGET_FILE:doccraft>
  ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/complex-table.doc)

# Standalone corpus builder for the 50 MiB performance envelope.
add_executable(perf_repack perf/repack_doc.cc cfb_test_util.cc)
target_link_libraries(perf_repack PRIVATE doccraft_cfb)
target_compile_features(perf_repack PRIVATE cxx_std_23)

# Repeat a validated structured body and its FKP pages for the 100 MiB tier.
add_executable(perf_repack_body perf/repack_body.cc cfb_test_util.cc)
target_link_libraries(perf_repack_body PRIVATE doccraft_cfb doccraft_doc)
target_compile_features(perf_repack_body PRIVATE cxx_std_23)

add_executable(m21_cli_fixture m21_cli_fixture.cc cfb_test_util.cc)
target_link_libraries(m21_cli_fixture PRIVATE doccraft_cfb doccraft_doc)
add_test(NAME m21_short_piece_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m21_cli.py $<TARGET_FILE:m21_cli_fixture>
  $<TARGET_FILE:doccraft> short-piece
  ${CMAKE_SOURCE_DIR}/tests/corpus/corpus/plain-text.doc)
add_test(NAME m21_missing_pool_cli COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_SOURCE_DIR}/tests/m21_cli.py $<TARGET_FILE:m21_cli_fixture>
  $<TARGET_FILE:doccraft> missing-pool
  ${CMAKE_SOURCE_DIR}/tests/fixtures/ole-embedded.doc)
