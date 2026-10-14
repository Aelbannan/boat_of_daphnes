# Importing textures from Wind Waker HD

BlueWake can use compatible model textures from your own USA Wii U
Wind Waker HD disc (`BCZE`, title `0005000010143500`). The importer converts
them into the same Dolphin-format replacement pack that Aurora loads in
Dusklight and BlueWake. Your GameCube `GZLE01` revision 0 disc is
still required to play.

## Where it is in the app

Mac, Linux, Windows, iPhone and iPad share the same section, first in their Mods tab or menu:
**Mods › Wind Waker HD textures**. It holds the **HD textures** switch, how
many textures the chosen pack has, and a way to add a pack:

- **Mac, Linux and Windows:** F1 › Mods › **Choose texture pack folder…**.
  Windows also has **Open the texture folder**.
- **iPhone and iPad:** ⋯ › Mods › **Install Texture Pack…**, which copies the
  pack into the app.

## Import in the app (Mac, Linux and Windows)

F1 › Mods › **Import from Wind Waker HD…** makes the pack inside BlueWake, with
nothing to install:

1. Pick the folder of your own decrypted Wind Waker HD dump: the one that holds
   `code`, `content` and `meta` (as Cemu uses it), or its `content` folder.
2. BlueWake reads it with the GameCube disc it already plays and makes the pack
   in a new folder, `Load/Textures/WWHD` in its data folder (`WWHD-2` and so
   on if that exists). A bar shows the progress; you can close the menu and play
   while it works, or cancel it.
3. When it finishes, reopen Mods to see the result. The new pack is selected
   and **HD textures** is on. Resume the game to save the choice, then restart
   BlueWake to load it.

It is the script below, built into the app: from the same discs it makes the
same 3,546 textures. The native importer takes about 30 seconds on an M3 Max. WUX and WUD
images still need the script. The same native entry is built for iPhone and
iPad in this draft, but an on-device import is still unverified. Android
integration remains part of the port work.

The output follows the app's data folder, including Linux portable mode and
an explicitly selected desktop settings location.

## The import script

The script makes the same pack from a computer, and also reads WUX and WUD
images with their key files.

## Set up the importer

From the repository root:

```sh
scripts/setup_wwhd_tools.sh
```

This creates a local Python environment in ignored `build/wwhd-tools`, installs
the versions pinned in `scripts/wwhd/requirements.txt`, and builds a small Yaz0
decoder if a C compiler is available. Without a compiler the Python decoder
works, but takes longer. No game data or keys are downloaded by this script.

On Windows, with Python 3.11 or newer installed, run this from PowerShell in the
repository instead (it builds the decoder with Visual Studio's C compiler when
one is installed):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_wwhd_tools.ps1
```

and use `build\wwhd-tools\Scripts\python.exe` wherever the commands below say
`build/wwhd-tools/bin/python`.

## Import your discs

For a WUX/WUD image, supply your disc key and a common key file. Keys are
16-byte binary files or text files containing 32 hexadecimal digits. The
importer decrypts the disc's own embedded ticket to obtain its title key.

```sh
build/wwhd-tools/bin/python scripts/import_wwhd_textures.py WindWakerHD.wux \
  --gc-disc WindWakerGC.iso \
  --disc-key game.key --common-key common.key --install
```

The image is read directly, so a 25 GB expanded WUD is unnecessary. The
importer validates the disc identity, file table checksum and each hashed
content block it reads. It also detects incomplete WUX extraction before
reading any texture content.

`game.key` and `common.key` beside the HD image are used by default. If you
already decrypted that disc's title key, use `--title-key title.key` instead
of `--common-key`. A NUS title key may differ from the disc's title key even
when the title ID is the same. A `.tik` file contains an encrypted title key;
it does not substitute for the WUD disc key.

For a decrypted HD `content` folder, no key files are needed:

```sh
build/wwhd-tools/bin/python scripts/import_wwhd_textures.py /path/to/content \
  --gc-disc WindWakerGC.iso --install
```

On a Mac, the default GameCube disc is the one inside your BlueWake app
(`/Applications/BlueWake.app/Contents/Resources/Game/GZLE01.iso`, or the same
place under `~/Applications`), or else the ISO Wind Waker Recomp imported
(`~/Library/Application Support/Wind Waker Recomp/GZLE01.iso`). You may omit
`--gc-disc` when one of those exists. On Linux it is the disc BlueWake
remembers (`~/.local/share/BlueWake/disc.txt`). On Windows it is the `.iso` or `.gcm` that
BlueWake remembers (`%APPDATA%\BlueWake\disc.txt`), or the ISO it unpacked
from a Dolphin `.rvz` (`%APPDATA%\BlueWake\GZLE01.iso`).

## Keep an existing pack as fallback

Add `--fallback-pack /path/to/existing-pack` to the import command to keep
existing HD artwork for anything the disc importer does not replace:

```sh
build/wwhd-tools/bin/python scripts/import_wwhd_textures.py WindWakerHD.wux \
  --disc-key game.key --common-key common.key \
  --fallback-pack "/path/to/ZWW4K 1.0.0d (4K)" --install
```

The importer searches nested texture folders, so the containing ZWW4K folder
is acceptable. Wind Waker HD artwork takes priority by source texture key,
regardless of PNG/DDS extension or mipmap naming. Only absent keys and their
own mipmaps are copied into a `fallback` subfolder. The original pack is
unchanged. The title copyright texture is excluded from fallback imports so
the disc's Nintendo copyright remains instead of a texture-pack banner. For example, existing HUD replacements can remain active while
compatible character and environment textures come from the HD disc.

## Use the result

On a Mac the default output is
`~/Library/Application Support/BlueWake/Load/Textures/WWHD`.
`--install` selects that folder in the app's settings
(`~/Library/Application Support/BlueWake/settings.ini`) for the next launch
and backs up the previous settings. Restart the app to load it. You can also
omit `--install` and pick the output folder with **Mods › Wind Waker HD
textures › Choose texture pack folder…**. Turn **HD textures** off to go back
to the GameCube textures.

On Linux the default output is `~/.local/share/BlueWake/Load/Textures/WWHD`
(`$XDG_DATA_HOME` when set), and `--install` writes the same setting to
`~/.config/BlueWake/settings.ini` (`$XDG_CONFIG_HOME` when set).

On Windows the default output is `%APPDATA%\BlueWake\Load\Textures\WWHD`.
Close BlueWake first: `--install` turns on **HD textures** and points it at
that folder in `%APPDATA%\BlueWake\settings.ini` (`hd_textures=1`,
`texture_pack=...`), keeping a backup of the previous file, and BlueWake
rewrites that file when its settings change. The settings menu (F1, Mods)
then names the pack's folder, with a button to go back to
`Load\Textures\GZLE01`.

Use `--output /path/to/new-folder` to choose another output folder. Existing
folders are preserved; the importer requires a new destination. For an
iPhone/iPad personal build, choose the generated folder with **Mods › Install
Texture Pack…**, turn on **HD Textures**, and restart.

The pack contains PNG textures, `_mipN` mipmaps and `import-report.json` with
source checksums, tool/dependency records, matches and skipped conflicts.
When a fallback is supplied, its selected keys and file checksums are also
recorded in the report. Unsupported fallback filenames are listed there.
The detailed report is local game-derived metadata; keep it with your private
pack. Never include the resulting textures, keys, tickets or discs in source
commits or public app releases.

## Scope and matching

The importer reads GameCube RARC archives, BTI textures and J3D TEX1 model
chunks, and HD Yaz0/SARC archives, Wii U BFRES v3/v4 FTEX textures, BFLIM
images and FFNT v3 bitmap fonts. Both Picto Box inventory icons and the water bottle have verified
mappings from the HD item layout to the GameCube item archive. The transparent
English HD title logo and its HD badge are fitted to the original logo UVs.
Other redesigned UI images are
not matched by their numbered filenames. For model textures it
matches both the resource identity and texture name; stage rooms are scoped
by stage and room, including the HD `sea_Room0` naming convention. It then
checks that the replacement preserves the original aspect ratio and is at
least the original resolution. The replacement filename hashes the original
GameCube texture bytes, and for indexed textures only the palette entries
those bytes reference, exactly as the renderer does.

The decoded HD component selections and source mipmaps are preserved, with
GameCube I4/I8 replacements using intensity for alpha as the original renderer
expects. Missing levels down to 1x1 are generated. Conflicting artwork for the same GameCube
texture key is skipped, except for explicitly audited shared-artwork variants.
Those choices preserve the original UV composition instead of HD debug-room,
figurine or photographic-cloud variants. Selected alternatives are recorded
in the report. Tiny placeholders and shader ramp textures stay
original. Different UV layouts or changed names need separate, verified
mappings and are not guessed.

This imports compatible model artwork. Wii U models, shaders, lighting,
normal/bake maps, redesigned HUD layouts and gameplay are not imported.
The HD camera mode indicators, minimap frame/check textures and BFRES
Japanese title layers use different transparency or layout rules and are excluded;
the English title uses the transparent HD layout images instead. An existing
fallback pack supplies other compatible missing textures. Verified BFLIM mappings
cover inventory bottles, equipment, trading items, collection icons and charts.
Forest Water's animated sparkle remains supplied by the GameCube game. HD HUD
hearts combine their separate outline and fill into the original sprite's UV
area. The swimming HUD's matching masks and decorations are also imported;
layout masks combine luminance and alpha for GameCube I4/I8 sampling.
Verified English action labels, inventory/quest/save headings, quest-log labels,
item counters, rupee digits and dungeon-floor labels are rebuilt from the HD
disc's own font glyphs. Each sprite retains its original alpha bounds and
transparent padding. A/B/X/Y symbols use the corresponding HD pictograph
font; redesigned L/R/Z and stick prompts retain their existing artwork.
The small HUD key and menu cursor corners are fitted the same way. Lower
resolution HD item-get sprites are not substituted for sharper originals.
The six opening story pictures are cropped to the original composition,
including splitting HD's panorama into the two original sprite planes.
RGBA8 and BC1-5 layout surfaces preserve color, luminance and alpha;
unmapped redesigned HUD images and JPA particle textures remain
unsupported. Unsupported resources and textures are listed in the report.

## Source and verification

The bounded format readers use the layouts documented by
[BFRES-Tool](https://github.com/aboood40091/BFRES-Tool),
[BFLIM-Tool](https://github.com/aboood40091/BFLIM-Tool), and
[JNUSLib](https://github.com/Maschell/JNUSLib). FFNT section layouts are also
documented by [3dstools](https://github.com/ObsidianX/3dstools/blob/master/bffnt.py);
the importer contains its own bounded reader for the audited BC4-alpha and
RGBA8 font surfaces, including GX2 array-page bank rotation. The Wii U address library is
vendored from BFRES-Tool at the exact revision in
`scripts/wwhd/vendor/SOURCE.json`, with its GPL-3.0-or-later license and
copyright notice. The importer has no GUI toolkit dependency.

Generated fixture checks cover decoding, palette hashing, stage/room scope,
malformed inputs, encrypted content integrity, conflicting replacements and
settings preservation, intensity-mask transparency, fallback priority and matching mip chains,
font array pages and malformed section chains, sprite-mask padding and panorama crops:

```sh
build/wwhd-tools/bin/python tests/wwhd_import_test.py
```

Checked by hand on October 12, 2026, on a Mac (M3 Max): a decrypted USA dump's
`content` folder with a revision 0 GameCube ISO gave 3,546 replacements (124
conflicting keys skipped). BlueWake loaded all of them at Outset, with the
Wind Waker HD look on and off; an unpaced run took 9.3 s against 9.2 s without the pack.

### Native importer memory check (October 15, 2026)

The importer now feeds PNG scanlines to zlib one row at a time, caps queued
and active PNG image data at 32 MiB (one larger image may run alone), and
shares unchanged mip chains across GameCube formats. Archive traversal borrows
uncompressed containers. The English layout pack retains only the heart
outline and HD badge needed by other members, then processes members in their
original order instead of keeping every member alive.

On the same M3 Max, using the same decrypted USA HD tree and revision 0
GameCube disc, a standalone native import with four PNG workers measured:

| Version | Peak resident RAM | Wall time |
| --- | ---: | ---: |
| PR #315 at `6232826`, before this change | 993 MiB (1,041,235,968 bytes) | 26.49 s |
| With streaming and shorter buffer lifetimes | 538 MiB (563,806,208 bytes) | 28.94 s |

Measured with `/usr/bin/time -l`, compiled with Apple Clang at `-O2`.
Peak RAM fell 46%. Other work was running on the Mac, so these times are not
an isolated speed benchmark. These are importer process measurements, not
the combined memory of an app importing while a game runs or an iPhone/iPad test.

Both native runs produced the same 3,546 texture keys, 33,861 filenames,
124 skipped conflicts and 17 unsupported entries. All 33,859 PNGs, including
every mip level, were byte-identical; matching metadata and all 91 generated
HUD/title textures were unchanged.

A fresh Python import from those inputs also matched every PNG's decoded
pixels and every texture chain's SHA-256, with the same matching metadata,
conflicts and unsupported entries. The native exact-report run produced the
same PNG bytes as its four-worker run, using 394 MiB peak resident RAM
(413,024,256 bytes) in 143.87 seconds with serial writing and SHA-256 enabled.

`tests/wwhd_native_test.cpp` checks generated nested SARC/RARC/Yaz0 archives,
empty members, malformed inputs and callback errors, plus streamed PNGs
against whole-image zlib compression and decoded pixels. It passes with
AddressSanitizer and UndefinedBehaviorSanitizer on this Mac. Build the development
importer and disc checks through the configured host build, so they use
the same pinned disc-crypto dependencies as the app. For an existing Mac
builder tree (replace `BUILD` with its CMake build directory):

```sh
cmake -S scripts/builder/training -B BUILD \
  -DBLUEWAKE_WWHD_TEST_PYTHON="$PWD/build/wwhd-tools/bin/python"
cmake --build BUILD --target bluewake_wwhd_native_import \
  bluewake_wwhd_disc_image_test bluewake_wwhd_disc_crypto_test \
  bluewake_wwhd_wiiu_disc_test bluewake_wwhd_import_job_test
ctest --test-dir BUILD/host --output-on-failure -R '^bluewake_wwhd_'
```

The test Python needs only `pycryptodome==3.23.0`; the app needs no Python.
Windows and Linux CI compile and run these generated-input checks too.
For a development import, run `BUILD/host/bluewake_wwhd_native_import`
with the HD content folder (or disc image), GameCube ISO and a new output
folder, in that order. An image can take three additional paths: disc key,
common key, title-key override (use an empty argument to skip the common
key). `game.key` and `common.key` beside the image are the defaults. Key
values never go in the command line. `WWHD_EXACT_REPORT=1` selects the slower,
serial SHA-256 report.
To compare native or Python packs, including every PNG and all available
RGBA-chain SHA-256s:

```sh
build/wwhd-tools/bin/python tests/wwhd_compare_packs.py OLD-PACK NEW-PACK
```

Keep both packs and their reports private. Windows/Linux compilation and
imports on phone/tablet hardware still need their own checks.

### Mac in-app check (October 15, 2026)

On the M3 Max with Metal, the native import was started through F1 > Mods
from the supplied decrypted USA content folder while a copied GameCube save
was running at Outset. Closing the menu let gameplay continue during the
conversion. The menu reported 3,546 textures and selected the resulting pack
in the isolated session's data folder. After selecting that installed pack,
resuming to save the preference and restarting, the renderer registered all
3,546 replacements; the HD HUD and textures were visible and movement worked.
The copied card's SHA-256 was unchanged.

The complete process peaked at 1,400,389,632 bytes (1,336 MiB) resident RAM,
sampled every 250 ms across startup, gameplay and import. This includes the
game and renderer and is not directly comparable to the standalone importer
table. It does not establish memory acceptance on iPhone, iPad or Android.

Keyboard navigation was used for the menu and the native folder picker.
Mouse clicks did not consistently activate the ImGui controls through the
automation surface; this run does not establish desktop mouse acceptance.

### Stopping an import

The desktop background job is owned until its worker exits. Closing the app
cancels and joins the worker before destroying its progress/status state;
completion, cancellation, a second import, missing input and shutdown are
covered by `bluewake_wwhd_import_job_test` on the desktop build paths. This
regression fails against the earlier detached worker and passes with AddressSanitizer
and UndefinedBehaviorSanitizer on the Mac. It uses generated inputs and a
controlled worker; it is not a mobile lifecycle or full-disc cancellation test.

### Native iOS Simulator check (October 15, 2026)

An isolated test app linked all eight production importer source files from
`a640108` for arm64 iOS Simulator 27.0 (iPhone 17e), using the iOS path's two
PNG workers. Seven generated-input cases passed: empty/missing HD folders,
missing GameCube disc, malformed-resource cleanup, successful import, refusing
to overwrite an installed pack, and repeat import to a new folder. Both packs'
12 PNGs passed independent CRC, dimension, pixel and mip-chain checks. The
production `BWGameOverlay.mm` also compiled without diagnostics.

This checks the importer in an actual Simulator process. The production menu,
Files picker, security-scoped external folder access, alerts, saved preferences
and next-launch renderer loading were not part of that harness. No game data
or physical device was used; full-dump iPhone/iPad memory acceptance remains open.

A separate full production simulator build from `a640108` with RecompCore
`ee99bbc` also linked and reached Outset on the iPad Air 11-inch (M4) / iPadOS
27.0 simulator. The game and touch overlay were visible. Its distinct test
bundle used a copied card, whose hash stayed unchanged. Device Hub returned
`noWindowsAvailable` for automation input, so this run did not verify the Mods
menu or file picker. The test app was uninstalled and the simulator shut down;
the physical iPad was not touched.

### Combined runtime check (October 15, 2026)

The native-import branch at `6bc56e5` also builds on macOS and iOS Simulator
with the newly approved Picto recognition pin, RecompCore `57123c6`. Five Mac
host checks passed (import-worker lifecycle, save state, cache flush, frame
interpolation and FIFO worker), as did the alpha, destination-alpha and early-depth
Metal pixel fixtures. A Mac Outset run registered all 3,546 replacements with
the HD look enabled and exited normally; the copied card stayed unchanged.
The temporary cache folder was missing in this smoke run, so SQLite caching
was disabled. This was not a performance measurement or first-photo test.
The exact combined iOS revision was compiled, not played or imported on hardware.

### Native WUX/WUD progress (October 15, 2026)

The native reader now handles WUD/WUX sector mapping, partitions, the Wii U
file table and title metadata, locally supplied keys, AES-CBC decryption,
and file-table / hashed-content checksums. It reads resources directly into
the existing texture matcher without expanding an entire WUD. The CLI can
use this path; app menus still select a decrypted folder.

The reader's generated fixtures cover reordered/duplicated sectors, reads
above 4 GiB, a 64 GiB logical image backed by less than 9 MiB of test data,
raw/CBC/hashed content across block boundaries, title/common/override keys,
wrong keys, malformed tables and damaged content. The encrypted fixtures are
checked independently by the existing Python reader. Mac CTest and the
parser's AddressSanitizer / UndefinedBehaviorSanitizer run pass. The updated
production Mac and iOS Simulator apps compile and link; Windows/Linux CI
results for this addition remain pending.

An end-to-end generated fixture also checks folder import against WUD and
mapped WUX in four key modes: nine successful imports have identical PNGs,
with every mip's dimensions, pixels and CRC independently checked. Twelve
failure cases verify wrong-key rejection, existing-pack preservation, and
staging cleanup after corrupt content. A fresh full import of the supplied
decrypted HD tree still matches all 3,546 keys and all 33,859 PNG bytes from
the earlier pack; this run peaked at 561,643,520 bytes RSS on the Mac.

Still required before advertising image support: an authentic image-to-pack
comparison, file/key pickers in each app, and
phone/tablet memory and lifecycle validation. The supplied reference is a
decrypted folder; generated encrypted fixtures do not establish acceptance
of an authentic disc dump. No game data or console keys are in these tests.
