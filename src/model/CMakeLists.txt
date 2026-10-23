add_library(doccraft_model STATIC document.cc)
target_include_directories(doccraft_model PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_model PUBLIC cxx_std_23)
