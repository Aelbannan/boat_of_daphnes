# Vendored bzip2

Decoder of WIA discs compressed with bzip2 (`runtime/host/src/disc_unpack.c`). Built as the static library `bluewake_bzip2`.

| Item | Value |
| --- | --- |
| Project | bzip2 and libbzip2, <https://sourceware.org/bzip2/> (Julian Seward) |
| Version | 1.0.8 (13 July 2019) |
| Source | the copy in Dolphin's `Externals/bzip2`, byte-identical |
| License | the bzip2 license (BSD-style), `LICENSE` here |

The library's `*.c` and `*.h` files are here unchanged, with `CMakeLists.txt`; the compressor is built along but unused.
