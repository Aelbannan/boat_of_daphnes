# Rights and licenses

## Boat of Daphnes' code

Boat of Daphnes is a fork of [BlueWake](https://github.com/chrissotraidis/bluewake) and, like it, is licensed under
the GNU General Public License, version 3 or (at your option) any later version. The full text is in
[LICENSE](LICENSE). BlueWake's history is kept in this repository and its authorship is unchanged.

GPLv3 is the license the parts are built from allow together:

- **RecompCore** (`runtime/recompcore/`, vendored from [RecompCore](https://github.com/chrissotraidis/RecompCore); see
  `runtime/VENDORED.md`), the compatibility runtime derived from [Dolphin](https://dolphin-emu.org/)
  (GPL-2.0-or-later). Its per-component licenses are under `runtime/recompcore/LICENSES/`; the Aurora renderer fork is
  MIT (`runtime/recompcore/GXRuntime/graphics/aurora/LICENSE`).
- **DolRecomp**, the translator, is fetched at a pinned commit at build time and keeps its own license.
- The recovered J3D rotation/translation formulas in `cmake/composite/native_j3d.c` are adapted from zeldaret/tww's
  [J3DTransform.cpp](https://github.com/zeldaret/tww/blob/09de0609ecdb6d30dd012e2258f755afdac1cb56/src/JSystem/J3DGraphBase/J3DTransform.cpp)
  at a revision under [CC0-1.0](https://github.com/zeldaret/tww/blob/09de0609ecdb6d30dd012e2258f755afdac1cb56/LICENSE).
  Function names and addresses in comments and documentation come from the same decompilation.

## Third-party code in this repository

| Component | Where | License |
|---|---|---|
| mGBA (the in-app Game Boy Advance) | `runtime/third_party/mgba` | MPL-2.0 |
| emibios (a free GBA BIOS, shipped unmodified as data) | `runtime/third_party/emibios` | LGPL-3.0-or-later |
| libarchive (disc image and archive reading) | `runtime/third_party/libarchive` | BSD-2-Clause |
| zstd, bzip2 (compressed disc images) | `runtime/third_party/zstd`, `runtime/third_party/bzip2` | BSD-3-Clause / GPL-2.0 (dual); bzip2 license |
| libwdi (GameCube adapter driver install on Windows) | `runtime/third_party/libwdi` | LGPL-3.0 |
| Mbed TLS (AES/SHA for the Wind Waker HD texture importer) | `runtime/recompcore/Externals/mbedtls` | Apache-2.0 |
| BFRES-Tool addrlib (Wii U surface addresses) | `scripts/wwhd/vendor` | GPL-3.0-or-later |
| Dear ImGui, SDL3, Dawn | through Aurora's build | MIT, zlib, BSD-3-Clause |

Each directory carries its license text and a `VENDORED.md` with the exact source and revision.

## What is not here

- **No game content.** *The Legend of Zelda: The Wind Waker*, its code, data, characters, names and imagery, and the
  GameCube trademark belong to Nintendo. This repository contains no disc image, game assets, saves, console keys or
  code translated from the game.
- **Your build is yours.** The module a full build makes (`gGZLE01_recomp.dll`) is translated from your own disc.
  Keep it for yourself; the software license grants no rights in game-derived code, and running a GPL-covered
  translator does not by itself place its output under the GPL.
- **Release builds.** Following BlueWake's practice, the maintainers publish a ready-made Windows build on the
  Releases page. It contains code translated from the game but no disc image, game assets, saves or console keys,
  and it needs the player's own disc to run. It will be taken down if the rights holder asks.

## Mods and data

- **Better Wind Waker** options (`mods/betterww/`): the option-site descriptions are derived from Better Wind Waker;
  each player's build translates those behaviours from their own disc. Better Wind Waker keeps its own terms.
- **Widescreen code** (`mods/widescreen/`): text of Dolphin's game-settings widescreen code and its variants.
- **The randomizer**: [wwrando](https://github.com/LagoLunatic/wwrando) (LagoLunatic, MIT) is fetched at a pinned
  revision and bundled with release builds together with its own Python runtime (PSF license); the randomizer
  integration in `scripts/rando/` and `windows/src/rando_*` is this project's.
- **The Bunny Hood** is generated from parameters by `scripts/rando/bunny/`; no model from another game is included.
- **DSP ROMs**: release builds carry Dolphin's free replacement DSP ROMs (`runtime/recompcore/Data/Sys/GC`), not
  Nintendo's.
- **Pipeline cache** (`initial_pipeline_cache.db` in release builds): Aurora rendering-pipeline descriptions recorded
  in testing (GPU state and usage order); no textures, models or game code.

## Inspiration

[Ship of Harkinian (Shipwright)](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (HarbourMasters) are the model for the
menus, settings, enhancements, trackers and Anchor online play; their code is not used. Better Wind Waker
(<https://github.com/WideBoner/betterww>, MIT) and wwrando are credited above.
