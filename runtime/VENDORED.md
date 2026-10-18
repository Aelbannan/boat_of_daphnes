# Vendored runtime

`runtime/recompcore/` is RecompCore, the Dolphin-derived runtime BlueWake builds against: GXRuntime, the
Aurora renderer fork, DSP sources and the StaticRecomp headers. It was imported on 2026-10-04 from:

| Item | Value |
| --- | --- |
| Repository | `https://github.com/chrissotraidis/RecompCore` |
| Commit | `0b86924175451edcea6f36a19b6ac6ca26aebc74` ("Smooth Motion: only meshes whose positions the game wrote blended vertex by vertex (the Mac's water path, at its old cost)", 2026-10-03) |
| Tree | `f7273c6e2abfc2e8d61535377391359a17579f67` |
| Files | 7,312 (7,291 regular, 21 executable), byte-identical to that commit |
| License | GPL-2.0-or-later as a whole (`COPYING`), with per-component licenses under `LICENSES/`; the Aurora fork is MIT (`GXRuntime/graphics/aurora/LICENSE`) |

The commit's 35 submodule links are not vendored: `DolRecomp` and 34 entries under `Externals/` (Qt, SDL,
FFmpeg, mGBA, libusb, hidapi, imgui, and others). The Windows build never used them.
- The renderer brings its own SDL3 and Dear ImGui through Aurora's CMake.
- DolRecomp, the translator, is fetched at its pinned commit into the git-ignored `ref/dolrecomp`.

From now on this copy is ordinary fork source: change it with normal commits. `patches/` records upstream's
history before the import and is not applied by anything.
