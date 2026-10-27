add_library(doccraft_crypto STATIC rc4.cc sha1.cc md5.cc derivation.cc xor.cc)
target_link_libraries(doccraft_crypto PUBLIC doccraft_base)
target_include_directories(doccraft_crypto PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(doccraft_crypto PUBLIC cxx_std_23)
