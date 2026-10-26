add_library(doccraft_doc STATIC
  fib.cc text.cc
  format/fkp.cc format/sprm.cc format/styles.cc format/lists.cc
  format/headers.cc format/notes.cc format/comments.cc format/bookmarks.cc format/formatted.cc format/formatted_helpers.cc
  format/pictures.cc)
target_link_libraries(doccraft_doc PUBLIC doccraft_model doccraft_base doccraft_miniz)
target_include_directories(doccraft_doc PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
target_compile_features(doccraft_doc PUBLIC cxx_std_23)
