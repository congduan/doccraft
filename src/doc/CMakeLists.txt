add_library(doccraft_doc STATIC
  fib.cc text.cc
  format/fkp.cc format/sprm.cc format/styles.cc format/lists.cc
  format/headers.cc format/formatted.cc)
target_link_libraries(doccraft_doc PUBLIC doccraft_model)
target_include_directories(doccraft_doc PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_doc PUBLIC cxx_std_23)
