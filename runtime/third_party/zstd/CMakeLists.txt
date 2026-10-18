# Zstandard 1.5.7 for BlueWake: only the decoder (lib/common and lib/decompress), for RVZ discs compressed with Zstandard
# (runtime/host/src/disc_unpack.c). Without the x86-64 assembly Huffman decoder. See VENDORED.md.
set(_zs ${CMAKE_CURRENT_LIST_DIR}/lib)
add_library(bluewake_zstd STATIC
    ${_zs}/common/debug.c ${_zs}/common/entropy_common.c ${_zs}/common/error_private.c ${_zs}/common/fse_decompress.c
    ${_zs}/common/xxhash.c ${_zs}/common/zstd_common.c
    ${_zs}/decompress/huf_decompress.c ${_zs}/decompress/zstd_ddict.c ${_zs}/decompress/zstd_decompress.c
    ${_zs}/decompress/zstd_decompress_block.c)
target_include_directories(bluewake_zstd PUBLIC ${_zs} PRIVATE ${_zs}/common ${_zs}/decompress)
target_compile_definitions(bluewake_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_MULTITHREAD=0 XXH_NAMESPACE=ZSTD_)
# Third-party code: no warnings of ours, and none of the host's force-included headers.
target_compile_options(bluewake_zstd PRIVATE -w)
