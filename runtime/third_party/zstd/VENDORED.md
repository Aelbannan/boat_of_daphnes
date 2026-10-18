# Vendored Zstandard (decoder)

Decoder of RVZ discs compressed with Zstandard (`runtime/host/src/disc_unpack.c`). Built as the static library `bluewake_zstd`.

| Item | Value |
| --- | --- |
| Project | Zstandard, <https://facebook.github.io/zstd/> (Meta) |
| Version | 1.5.7 |
| Source | <https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz>, SHA-256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3` |
| License | dual BSD-3-Clause / GPL-2.0; used under the BSD-3-Clause, `LICENSE` here |

Only `lib/common` (without the multithreading pool, built out) and `lib/decompress` (without the x86-64 assembly Huffman decoder,
`ZSTD_DISABLE_ASM`), with `zstd.h` and `zstd_errors.h`, unchanged. Everything else of the release is left out.
