add_library(doccraft_doc STATIC fib.cc text.cc)
target_include_directories(doccraft_doc PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_doc PUBLIC cxx_std_23)
