# Vendored mGBA

The Game Boy Advance core behind the Tingle Tuner (`runtime/host/src/gba_link.c`).

| Item | Value |
| --- | --- |
| Project | mGBA, <https://github.com/mgba-emu/mgba> (Jeffrey Pfau and contributors) |
| Version | commit `0b40863f64d0940f333fa1c638e75f86f8a26a33` of `master` ("CMake: Bump minimum version to 3.12", 2025-11-04), the commit Dolphin's current `Externals/mGBA/mgba` submodule pins |
| License | MPL-2.0 (`LICENSE`). The files stay under MPL-2.0 with their notices; the rest of BlueWake is GPL-3.0-or-later, which MPL-2.0 section 3.3 allows (no file carries the "Incompatible With Secondary Licenses" notice). The source of this library is this folder: it is unmodified. `src/third-party/inih` is New BSD (its `LICENSE.txt`). |
| Built as | the library only: `LIBMGBA_ONLY`, no zlib/lzma/png, GBA core only (`windows/CMakeLists.txt`), the way Dolphin builds it |

## What is here

`include/`, `src/{arm,core,debugger,feature,gb,gba,script,sm83,util,tools}`, `src/platform/{cmake,posix,windows,test}`
(CMake lists some of these unconditionally), `src/third-party/inih`, and the top-level `CMakeLists.txt`,
`version.cmake`, `LICENSE`, `README.md`, `CHANGES`, byte-identical to that commit. Not taken: the frontends
(`src/platform/{qt,sdl,libretro,...}`), `src/third-party/{zlib,lzma,libpng,sqlite3,discord-rpc}`, `cinema/`, `res/`,
`doc/`, `opt/`, the Windows installer files.
