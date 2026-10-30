add_library(doccraft_base STATIC charset.cc)
target_include_directories(doccraft_base PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_base PUBLIC cxx_std_23)

# Compressed [MS-DOC] pieces are encoded in the document's ANSI code page.
#
# The decoder picks its backend at compile time (src/base/charset.cc): iconv
# where the platform has it, MultiByteToWideChar on Windows, the built-in
# CP1252 table where neither exists. iconv is part of libc on glibc and on
# Apple's libSystem, so the header is the capability test and the library is
# only linked when it exists as a separate one (GNU libiconv, busybox).
# The previous condition additionally required the library on APPLE, which
# silently disabled multibyte decoding on macOS when find_library() missed the
# SDK stub.
find_path(DOCCRAFT_ICONV_INCLUDE_DIR iconv.h)
find_library(DOCCRAFT_ICONV_LIBRARY NAMES iconv)
if(NOT WIN32 AND DOCCRAFT_ICONV_INCLUDE_DIR)
  target_compile_definitions(doccraft_base PRIVATE DOCCRAFT_HAVE_ICONV)
  if(DOCCRAFT_ICONV_LIBRARY)
    target_link_libraries(doccraft_base PRIVATE ${DOCCRAFT_ICONV_LIBRARY})
  endif()
  message(STATUS "charset: iconv decoding enabled")
elseif(WIN32)
  message(STATUS "charset: MultiByteToWideChar decoding enabled")
else()
  message(STATUS "charset: no platform decoder, CP1252 fallback only")
endif()
