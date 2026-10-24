#  imgwz replaces the vendored NoLifeNx reader.  It needs zlib for canvas payloads and
#  the AES-256 keystream is computed in img_cipher.cpp, so there is no crypto library to
#  link; the file mapping is plain mmap/MapViewOfFile.
add_library(imgwz STATIC
    audio.cpp
    bitmap.cpp
    img_cipher.cpp
    img_file.cpp
    node.cpp
    nx.cpp)
target_include_directories(imgwz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(imgwz PUBLIC cxx_std_17)
target_link_libraries(imgwz PUBLIC z)
