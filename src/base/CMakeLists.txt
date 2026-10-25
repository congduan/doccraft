add_library(doccraft_base STATIC charset.cc)
target_include_directories(doccraft_base PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_base PUBLIC cxx_std_23)

# Compressed [MS-DOC] pieces are encoded in the document's ANSI code page.
# iconv is a libc facility (glibc builtin, libiconv elsewhere); it is optional
# and the decoder falls back to CP1252 when it is missing.
find_path(DOCCRAFT_ICONV_INCLUDE_DIR iconv.h)
find_library(DOCCRAFT_ICONV_LIBRARY NAMES iconv)
if(DOCCRAFT_ICONV_INCLUDE_DIR AND (DOCCRAFT_ICONV_LIBRARY OR NOT APPLE))
  target_compile_definitions(doccraft_base PRIVATE DOCCRAFT_HAVE_ICONV)
  if(DOCCRAFT_ICONV_LIBRARY)
    target_link_libraries(doccraft_base PRIVATE ${DOCCRAFT_ICONV_LIBRARY})
  endif()
  message(STATUS "charset: iconv decoding enabled")
else()
  message(STATUS "charset: iconv not found, CP1252 fallback only")
endif()
