add_library(doccraft_cfb STATIC cfb_reader.cc cfb_writer.cc)
target_link_libraries(doccraft_cfb PUBLIC doccraft_base)
target_include_directories(doccraft_cfb PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_cfb PUBLIC cxx_std_23)
