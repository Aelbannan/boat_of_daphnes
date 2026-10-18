# bzip2 1.0.8 for BlueWake: the decoder of WIA discs compressed with bzip2 (runtime/host/src/disc_unpack.c). The whole library is
# built, as released. See VENDORED.md.
add_library(bluewake_bzip2 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/blocksort.c ${CMAKE_CURRENT_LIST_DIR}/bzlib.c ${CMAKE_CURRENT_LIST_DIR}/compress.c
    ${CMAKE_CURRENT_LIST_DIR}/crctable.c ${CMAKE_CURRENT_LIST_DIR}/decompress.c ${CMAKE_CURRENT_LIST_DIR}/huffman.c
    ${CMAKE_CURRENT_LIST_DIR}/randtable.c)
target_include_directories(bluewake_bzip2 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
# Third-party code: no warnings of ours, and none of the host's force-included headers.
target_compile_options(bluewake_bzip2 PRIVATE -w)
