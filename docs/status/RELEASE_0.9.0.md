# BlueWake 0.9.0 release record

The working record for 0.9.0, started October 24, 2026. It says what the next version includes, what waits, and every
check before publishing. Fill in each check with the device, the commit and what was seen; a row stays "not yet"
until someone runs it. Previous release: [0.8.0](RELEASE_0.8.0.md), October 18. Only Chris publishes.

`main` is not frozen. `version.json` stays 0.8.0 until Chris chooses the candidate after his review on Sunday,
October 25; the freeze pull request then sets 0.9.0, build 8.

## The aim

Fix the bugs players hit most (the camera jitter with the direct right-stick camera, the Smooth Motion flicker),
make Mac, iPhone and iPad as fast as the PC builds, ship the Linux AppImage the Dawn fix unblocks, and carry the HD
texture importer and the one desktop menu that are already in `main`.

## Test candidate

| | |
| --- | --- |
| Version | 0.9.0, build 8 (set by the freeze pull request) |
| Test candidate | `f55ffba5`, a local integration branch (October 24): `main` at `729298f` plus #455, #456, #398 (which carries #371 and #446), #431 and #450. It exists so the Mac and iPad can be played and measured before the merges; the release commit will be `main` after them |
| RecompCore | `1446450`, #398's pin: RecompCore#44 (the Dawn callbacks, on #43), then #40 (Smooth Motion), then #42 (sun shadows), on `bluewake-next` |
| Previous release | 0.8.0, October 18 (`f2611b1`) |

## What it contains

### Already in `main`

| Change | Issue | Ships to | Checked so far | Still to check |
| --- | --- | --- | --- | --- |
| **One desktop menu** for Mac, Linux and Windows, drawn from one table of every setting: the Controls tables, pausing under the menu as a setting, one control for every list, contrast and text size, greyed settings that say why, a Config tab and controller navigation (pdale-boop, #384 to #388) | #320 | Windows, Mac, Linux | CI on all three; merged October 20 | The menu with a controller on the Mac (Sunday); the Windows rows in [WINDOWS_TASKS.md](../WINDOWS_TASKS.md) |
| **Share Session Log** sends this session's log and the one before, so a crash can be reported after relaunching (#440) | none | Every platform | CI | A crash, a relaunch and a share on one platform by hand |
| **HD textures from a real Wind Waker HD disc image:** the importer reads the hashed content a WUD/WUX stores (#443), takes a key file, a pasted key or an emulator `keys.txt` as it is, keeps the form after a failure, and colours the result (#444) (jkoehler11) | #336 | Every platform but Android | Importer and parser tests; a USA WUX on Linux with the player's `keys.txt`: 3,546 textures imported, 124 skipped for a taken name, 17 unreadable | A Windows import from the menu; an iPhone or iPad import with a `keys.txt` |
| The Android builder runs on macOS too (benwolfram, #391), and Android training refuses a run that never reaches player control (#445) | #75 | Android (experimental, own-disc) | Tests; the Mac run to a signed APK | The builder on a Windows PC; fresh `--retrain` measurements on a phone |
| A sea compass switch, off by default: on, the red end points the way you're sailing, on a north-up dial (jslay88, #456, merged October 24) | none | Every platform | Linux screenshots on and off at all four headings | The clock hand; a Windows sail |

### In the test candidate, waiting on Sunday's review

Each of these changes gameplay, timing or rendering, so it merges after it has been played on the platform it
affects (AGENTS.md).

| Change | Issue | Ships to | Checked so far | Still to check |
| --- | --- | --- | --- | --- |
| **The camera no longer dips for a frame every few seconds** with Direct right-stick camera and aiming on: the feature hooks also run on the block a host turn starts on (jslay88, #455) | #401, #430 | Every platform | Linux: before, a dip every 2 to 15 seconds standing still, each one a frame where the camera hook did not run; after, none in a play session. The dispatch and handoff tests fail without the fix. Mac test candidate, October 24, a scripted stand-still from the tour's Outset state (the right stick nudged, then no input): 0 of 5,410 frames off the held camera view in three minutes; with only #455's host change reverted, 20 of 5,103, about one every 8.5 seconds, each a single frame at the game's own pitch. iPad Pro, the test candidate: 0 of 1,843 | Mac and iPad by hand: the camera in normal play, then climbing, quick doors and the draw tags. Windows: the row in WINDOWS_TASKS.md. Both reporters confirm before their issues close |
| **Smooth Motion without the flicker** in the HD look, clouds, wave crests and swimming (pdale-boop, #371, RecompCore#40) | #136 | Every platform | The contributor's platform checks; CI | The flicker cases above 60 FPS on the Mac and iPad; Windows |
| **Sun shadows from a shadow map,** so what is off screen still casts, in the HD look (off by default) (pdale-boop, #398, RecompCore#42) | #57 | Every platform but Android | The contributor's checks; CI. Mac test candidate, October 24: frames at all five tour stops with the HD look, with and without sun shadows, no artifacts; two runs of each are identical. Turning sun shadows on changes the guest state from the first checkpoint, the same way every run: on the high quality it widens the palm trees' cull boxes in game memory and the room model's culling, so shadows from off screen still cast. Off by default, so default builds are unaffected | The HD look on the Mac and iPad by eye, with the quality switches |
| **Linux builds at the player's CPU target again:** Dawn compiles with BMI2 (jslay88's RecompCore#43, with #44 for the pinned Apple and Android headers; #446 pins it and runs Linux CI with `-march=x86-64-v3`) | #433 | Linux (and every build, through the pin) | Linux/AppImage CI with the player's flags, Windows host, sanitizers; the callbacks compiled for Mac, iOS, tvOS and Android. Mac test candidate, October 24: the existing Dawn and pipeline caches (made by older builds on October 1) loaded and were written again over seven launches on Metal, with no cache errors | The restart check on Windows; a contributor's own-disc AppImage |
| **Mac, iPhone and iPad as fast as the PC builds:** the Windows and Linux module preparations and behaviour timing by default (`--conservative` keeps the plain translation) (#431) | #104 | Mac, iPhone, iPad | October 20: 64 to 77% more game frames a second on the M3 Max and 75 to 81% on the iPad Pro ([record](APPLE_RECIPE_TOUR_2026-10-20.md)). October 24 test candidate on the iPad Pro: the same gain, +75 to 79% over 0.8.0 (checks below) | Chris plays the Mac and the iPad by hand: a cutscene with music, saving and loading, the menus, the Picto Box, the HD look |

### Tools and records (nothing changes for players)

The benchmark tour keeps one guest state for repeated runs (#450) and will refuse a setup run that never reaches
gameplay (#459, for #458); the Steam Deck cache-flush and GX-worker records (#452, #453); what an Outset frame is made
of (#454); the Deck game-thread plan and its finished no-gain experiments (#421); the worker tests (#424); the
native and translated code guide (#435).

## Not in 0.9.0

| What | Why it waits |
| --- | --- |
| Android first-run screen and crash log (#460) | To be split into the screen and the log, with the disc image in its ready check, then run on a phone |
| Android HD import (#347) | A phone and lifecycle pass |
| The cold first Picto Box photo (#342, RecompCore#39) | Conflicts with `main` and its platform checks |
| iPhone and iPad menu switches that keep the menu open (#316) | A tap test on the iPad |
| The Mac's slow-chunk compile flags (#392) | With #431's defaults only 5 of 859 chunks take over three minutes and the module compiles in 36 minutes, so it would save little and cost about 1.5% of game speed; likely parked |
| Natives rounds 4, 5 and 7 (#179) | They cannot be generated from a current tree (#451); the set owners first |
| Clang 18 for the Deck (#437) | Its rendered and Game Mode measurements are still running |
| Building the iPhone and iPad app on Windows (#100) | Draft; its own platform checks |

## Release notes (draft, for the release page)

A draft in Chris's voice, for the changes Sunday's review accepts; drop any line whose pull request does not merge.

BlueWake 0.9.0 makes the Mac, iPhone and iPad builds as fast as the PC builds, fixes the camera dipping with the
right-stick camera, and stops the flicker with Smooth Motion. Every version needs your own USA (GZLE01) disc image.
Nothing from the game is included except in the ready-made Windows and Linux builds.

- **Much faster on Mac, iPhone and iPad.** Builds you make with PadMint or the builder now get the same module
  preparations and behaviour timing as the Windows and Linux builds: on the benchmark tour, 75 to 79% more game frames
  a second on an iPad Pro (M2) and 64 to 77% on an M3 Max, with a smaller game module and a much shorter build.
  `--conservative` builds it the old way.
- **Fixed: the camera dipping for a frame every few seconds** with Direct right-stick camera and aiming on (thanks to
  jslay88).
- **Fixed: flicker with Smooth Motion above 60 FPS** in the HD look, the clouds, wave crests and swimming (thanks to
  pdale-boop).
- **Sun shadows from a shadow map** in the HD look, so what is off screen still casts a shadow (pdale-boop). Off by
  default, under the HD look's settings.
- **One menu on every computer** (thanks to pdale-boop): Windows, Mac and Linux draw the same menu from one list of
  settings, with controller navigation, contrast and text size, a Config tab, greyed settings that say why, and
  pausing under the menu as a setting.
- **HD textures from a real WUD/WUX image:** the importer reads the content a real image stores, takes a key file, a
  pasted key or an emulator `keys.txt` as it is, keeps the form after a mistake, and says what it imported and
  skipped (thanks to jkoehler11).
- **Share Session Log** includes the session before, so a crash can be reported after relaunching.
- **Linux** builds at the player's CPU target again (thanks to jslay88).
- **New, off by default:** a sea compass switch: the red end points the way you're sailing (jslay88).
- **Android, experimental:** the builder runs on macOS too (thanks to benwolfram).

Known issues: the Wind Temple's rooms R04 and R08 need an opt-in switch to load (#318). The Windows download isn't
code-signed; its SHA-256 goes here, with [If Windows says it's a virus](../WINDOWS.md#if-windows-says-its-a-virus).

## After publishing

Post on each issue that 0.9.0 has the change and ask the reporter to confirm. Close an issue only when the reporter
does, or under the 14-day rule (AGENTS.md).

| Issue | What to ask |
| --- | --- |
| #401, #430 The camera dip | Whether the camera holds still with Direct right-stick camera and aiming on |
| #136 Flickering clouds and waves | Whether the flicker is gone with Smooth Motion above 60 FPS |
| #433 Linux build at x86-64-v3 | Whether the container and AppImage build now finish |
| #104 Built BlueWake with PadMint? | How long the build took and how it runs, on which iPhone, iPad or Mac |
| #336 Texture pack status | Whether the import's result and the loaded count are clear |
| #57 The Wii U HD look | Whether the sun shadows look right with the HD look |


## Checks before publishing

| Check | Who | Result |
| --- | --- | --- |
| Sunday's review on the Mac and the iPad: the camera with the direct camera on, Smooth Motion, the HD look's sun shadows, the Apple defaults played by hand, the desktop menu with a controller | Chris | Not yet |
| Merge in order: RecompCore#43 and #44, #40, #42 into `bluewake-next`; then #446, #371, #398, #455 and #431 into `main` (#456, off by default, merged October 24); then the freeze pull request | Chris decides; Codex merges | Not yet |
| The benchmark tour on the candidate against 0.8.0 | Codex | iPad Pro (M2), October 24: the test candidate (game module `00028fb0…`, built from `f55ffba5` with #431's defaults and the Mac build's training profile) against the October 20 runs of 0.8.0 as players have it, with the same harness, card and settings. Headless game frames a second, two runs: Outset 91.1 / 91.9 (0.8.0: 52.2 / 52.1, +75%), Windfall 92.6 / 92.6 (52.0 / 53.0, +76%), Dragon Roost Cavern 134.3 / 135.8 (75.1 / 77.0, +78%), Forest Haven 75.6 / 76.1 (42.3 / 42.6, +79%), the sea by the Fortress 68.5 / 68.2 (39.1 / 39.0, +75%). Every stop loaded, no dips. At the game's own speed every stop holds 30 game frames a second with the game thread 34 to 52% busy (0.8.0: 51 to 88%). Within 1.2% of the October 20 recipe build, so the rendering stack and the camera fix cost nothing measurable here. The player's card hashed the same (`4b54ec25…`) before the install and after every run. The test candidate stays installed for Sunday; the 0.8.0 app is kept to reinstall |
| The 468-place warp sweep (`scripts/stage_select_sweep.py`) on the candidate | Codex | Mac (M3 Max), October 24: 446 of 468 arrive and the card is unchanged after all 468, the same outcome at every place as the 0.8.0 candidate on October 17 (an early file at Outset, a copy of the iPad's card). The other 22 are the names file's test-room hangs, Wind Temple R04 and R08 (#318), and late-game cutscenes the file can't reach. A rendered tour on the Mac loaded all five stops at 30 game frames a second without dips, with the standard look and with the HD look (with and without sun shadows) |
| Windows build from the frozen commit on Chris's PC, with the checks in [WINDOWS_TASKS.md](../WINDOWS_TASKS.md) | Chris's PC | Not yet |
| Linux AppImage from a contributor's own disc, after #446, with `check_appimage_abi.py` | cforain or jkoehler11 | Not yet |
| The app-only IPA, PadMint's `audit`, and PadMint's `scripts/player-check.sh` before tagging and after publishing | Codex, on this Mac | Not yet |
| `scripts/release/check_public_assets.sh` on every artifact | Codex | Not yet |

