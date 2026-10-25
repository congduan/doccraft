add_library(doccraft_base INTERFACE)
target_include_directories(doccraft_base INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_base INTERFACE cxx_std_23)
