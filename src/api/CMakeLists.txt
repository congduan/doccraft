# Buffer-in/buffer-out embedding entry point for WebAssembly, Android and iOS.
# It links only the four core layers and the platform-free base helpers, so it
# builds wherever those do - no files, no stdio, no command line.
add_library(doccraft_api STATIC doccraft_api.cc)
target_link_libraries(doccraft_api PUBLIC doccraft_cfb doccraft_doc
  doccraft_model doccraft_docx)
target_include_directories(doccraft_api PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_api PUBLIC cxx_std_23)
target_compile_definitions(doccraft_api PRIVATE
  DOCCRAFT_VERSION="${PROJECT_VERSION}")
