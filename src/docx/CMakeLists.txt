add_library(doccraft_docx STATIC writer.cc)
target_include_directories(doccraft_docx PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(doccraft_docx PUBLIC doccraft_model)
target_compile_features(doccraft_docx PUBLIC cxx_std_23)
