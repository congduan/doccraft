add_library(doccraft_docx STATIC empty.cc)
target_include_directories(doccraft_docx PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_docx PUBLIC cxx_std_23)
