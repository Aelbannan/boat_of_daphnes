# Vendored libarchive

Archive reading for mods (`.zip`, `.rar` including RAR5, `.7z`) in the Windows app. The wrapper that
the rest of BlueWake calls is `runtime/host/src/mod_archive.h`.

| Item | Value |
| --- | --- |
| Project | libarchive, <https://www.libarchive.org/> (Tim Kientzle and contributors) |
| Version | 3.8.9, the latest stable release when imported (2026-10-05) |
| Source | <https://github.com/libarchive/libarchive/releases/download/v3.8.9/libarchive-3.8.9.tar.gz> |
| SHA-256 of the archive | `f5a6539059cf5e597dbeda37bfa4874b1e8dea063c8d93bf85a2b44af90a5bd4` |
| License | BSD-2-Clause as a whole (`COPYING`, kept unchanged); `archive_parse_date.c` is public domain, the four BLAKE2 files are CC0 1.0 / OpenSSL / Apache 2.0 at your choice, `archive_read_support_filter_compress.c` also carries a BSD 3-clause UC Regents notice |

## What is here

- `COPYING`: libarchive's, unchanged.
- `libarchive/`: every `*.c` and `*.h` of the release's `libarchive/` directory (173 files), byte-identical
  except for the one local patch below. Only the sources listed in `CMakeLists.txt` are compiled: the read side of zip, 7z, RAR and
  RAR5 with what they need (string conversion, entry, PPMd, BLAKE2, the Windows layer). The rest
  (tar, cpio, ISO, CAB, LHA, XAR, mtree, the write side, disk extraction, the gzip/bzip2/xz/lz4/zstd
  stream filters) stays on disk so the folder can be refreshed from a release by copying `libarchive/*.c`
  and `*.h` over it, but is not built.
- `CMakeLists.txt`: BlueWake's own, a static library `bluewake_archive`. libarchive's CMake is not used.
- `config_bluewake.h`: BlueWake's own. It is the `config.h` libarchive's CMake generated for this
  toolchain (Visual Studio's clang 20.1.8, MSVC ABI, Windows SDK), stripped of comments and unset
  macros, plus the zlib and liblzma switches. To regenerate it, configure libarchive 3.8.9 with CMake
  with the features marked off in the table below disabled, and compare.

## Configuration

| Feature | State |
| --- | --- |
| zip, 7z, RAR (v4), RAR5 readers | on (RAR, RAR5, PPMd and the 7z branch filters are libarchive's own code) |
| Deflate (zip, 7z) | zlib: Aurora's zlib-ng in compat mode (`ZLIB::ZLIB`) |
| LZMA, LZMA2, BCJ/ARM/PPC/SPARC/IA64 branch filters (7z, zip) | liblzma from `runtime/recompcore/Externals/liblzma` (`LibLZMA::LibLZMA`); see below for what was added to it |
| bzip2 | off: `Externals/bzip2` in the runtime has no sources (a submodule that is not vendored), so bzip2-compressed entries fail to read |
| zstd, lz4, OpenSSL, libxml2, expat, iconv, libb2, nettle, mbedtls | off; names convert with the Win32 API, BLAKE2 is libarchive's reference code |
| Digests, ZIP AES / RAR5 encryption | Windows CNG (`bcrypt`) |
| ACLs, extended attributes, write support, bsdtar/bsdcpio/bsdcat/bsdunzip | not built |

Encrypted archives are not a goal: the wrapper has no passphrase call, so encrypted entries fail to read.

## Tested (2026-10-05)

Through `tests/mod_archive_test.c`, entry bytes compared with 7-Zip 24.09, Python's zipfile or an earlier extraction: zip
(deflate, LZMA, PPMd), RAR v4, RAR5, 7z (LZMA, LZMA2, LZMA2 + BCJ, Deflate, PPMd, solid and not, compressed
headers), a non-ASCII archive path and entry names.

## Removed from the release tarball

`CMakeLists.txt`, `configure`, `Makefile.*`, `aclocal.m4`, `build/` (autotools and CMake scaffolding),
`cat/`, `cpio/`, `tar/`, `unzip/`, `libarchive_fe/`, `test_utils/`, `libarchive/test/` (the unit tests),
`contrib/`, `doc/`, `examples/`, `INSTALL`, `NEWS`, `README.md`, the man pages (`libarchive/*.3`, `*.5`).

## Local changes

1. `libarchive/archive_read_support_format_7zip.c`, PPMd case of `decompress()` (marked `BLUEWAKE PATCH`):
   the bytes the PPMd range decoder takes past the end of the input buffer (`ppmd_read` falls back to
   `__archive_read_ahead`) are counted as used. Upstream 3.8.9 does not, so the next call decodes them again
   and a PPMd stream whose input runs out before its output (incompressible data: PNGs, `.arc` files) fails
   with "Failed to decode PPMd". With the patch, 7-Zip 24.09's PPMd archives of such data read back
   bit-exact. Upstream issue not filed; drop the patch if a later release carries the fix.
2. `runtime/recompcore/Externals/liblzma` (RecompCore's xz 5.2.4 subset, used for the LZMA decoder) had no
   branch filters, so 7z's default for executables (BCJ x86 + LZMA2) failed with "Invalid options". Added
   `simple/{x86,arm,armthumb,ia64,powerpc,sparc}.c` from xz 5.2.4
   (<https://tukaani.org/xz/xz-5.2.4.tar.gz>, SHA-256
   `b512f3b726d3b37b6dc4c8570e137b9311e7552e8ccbab4d39d47ce5f4177145`; public domain; the rest of `simple/`
   is byte-identical to it), listed them in that folder's `CMakeLists.txt` and enabled their
   `HAVE_DECODER_*` and `HAVE_ENCODER_*` switches in its `config.h`.

## Known limits

- bzip2 entries (zip method 12, 7z BZip2) and zstd/lz4 are not readable (no library); encrypted entries fail.
- PPMd combined with the x86 BCJ filter on an executable (`7z a -m0=PPMd -mf=BCJ`) still fails to read in
  upstream's own code path; PPMd alone, LZMA, LZMA2, Deflate, Copy, and LZMA2 + BCJ work.
- Deflate64 and other exotic zip methods are unsupported by libarchive.
