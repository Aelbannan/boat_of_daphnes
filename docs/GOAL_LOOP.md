# What we're doing

The dated plan, and the five-step loop every session follows. Owner: Chris. Updated October 20, 2026. The ranked
backlog is [PRIORITIES.md](PRIORITIES.md), the speed strategy and its runbook are [PERFORMANCE.md](PERFORMANCE.md),
and the standards everything is held to are in [DIRECTION.md](DIRECTION.md). The daily sections from October 11 to
15, with every measurement, are kept in [status/GOAL_LOOP_2026-10-15.md](status/GOAL_LOOP_2026-10-15.md).

**The aim for the next versions:** BlueWake runs faster and steadier, the bugs players reported are fixed, the Wind
Waker HD look and textures work the same way on every build, Linux becomes a download, and Android gets to the
iPhone app's standard.

## The loop

Every session, by a person or a bot, runs the same five steps:

1. **Read** today's section, then check `main`, open pull requests and new issue replies.
2. **Do** the first row that isn't done and isn't waiting on someone. Before writing anything new, check whether an
   open pull request, or Elliott's `windows-release`, already does it.
3. **Prove** it. Say what ran, on what device, from which commit. A build that compiles is not a game that plays.
4. **Record** it. Mark the row done here, add numbers to [PERFORMANCE.md](PERFORMANCE.md#results), and tell the
   reporters on their issues, in Chris's voice.
5. **Re-plan** when the evidence changes. Unfinished rows move to the next day. Every row has a "done when".

## Speed, in three horizons

The full reasoning is in [PERFORMANCE.md](PERFORMANCE.md#the-plan). In short:

| Horizon | When | What | Gate |
| --- | --- | --- | --- |
| 1. Lean what we have, and catch up with Elliott | 0.7.0 and the next two weeks | Lean block copies (the default since 0.7.0), draw fusion, his faster loads and natives rounds 4, 5 and 7, his upload changes, hidden symbols, Smooth Motion pacing | Faster on the benchmark, and the game plays the same |
| 2. Behavior, not cycles | 0.8 (weeks) | Lighter timing, natives that remove round trips, cached display lists | The benchmark, and plays the same |
| 3. Follow the decompilation | Months | Natives compiled from its source, drawing at the GX/J3D API level, matched scenes ported | Each piece checked against the recompilation |

## 0.9.0 on Saturday, October 24

`main` stays releasable: every pull request passes CI, and anything that changes gameplay, timing or rendering is
played on the platform it changes before it merges. Until Friday, merge what is ready, in the order below. On Friday
evening `main` freezes for 0.9.0, and only docs merge until the release is out. The Windows build is made once, on
Saturday, from the frozen commit; until then the Windows CI checks every merge.

| Day | What | Who |
| --- | --- | --- |
| Tuesday to Thursday, October 20 to 22 | The merge window, in the order below | Contributors; Chris plays and merges |
| Friday, October 23 | Freeze: `version.json` 0.9.0 and the candidate commit. Overnight on the Mac: the app-only IPA, PadMint's `player-check.sh` before tagging, the benchmark tour against 0.8.0 and the 468-place sweep | Chris decides; the Mac runs it |
| Saturday, October 24 | The Windows build and its play checks on Chris's PC, a Linux AppImage if a contributor has built one, then publishing and `player-check.sh` after publishing | Chris |

In merge order:

1. **Tests and docs that change nothing in the game:** #397 (the native work pool tests) and #428 (the iPad tour)
   merged on October 20.
2. **One desktop menu,** #384 to #388 (pdale-boop): Chris's hands-on Mac pass with a controller, then the five in
   order. They merge cleanly into `main`.
3. **Smooth Motion without the flicker** (#371, RecompCore#40), then **sun shadows from a shadow map** (#398,
   RecompCore#42): Chris looks at both on the Mac, the two RecompCore pull requests merge into `bluewake-next`, and
   the BlueWake pull request that merges second is rebased on the first. Both pin RecompCore, and #398 also touches
   `tests/test_safe_mode_hd.py`, as #388 does.
4. **The Mac and iOS speed default:** the Windows and Linux recipe made the game 64 to 81% faster on the M3 Max and
   the iPad Pro ([record](status/APPLE_RECIPE_TOUR_2026-10-20.md)). One pull request, after Chris plays the test
   builds by hand. If that hasn't happened by Thursday, it stays opt-in for 0.9.0.
5. **#392** (jslay88, the Mac compile flags on the slow chunks only): a tour run on the M3 Max against `main`.

After 0.9.0: #424 (counters in the game module's native math), #347 (the Android HD import, which needs a phone),
#342, and natives round 7 on the benchmark tour, which needs one extra Windows build.

## Where things stood on October 17

**The latest download is 0.8.0** (published October 18 from `f2611b1`): the Windows zip, the iPhone and iPad app for
PadMint, the recipe and the source. [Its release record](status/RELEASE_0.8.0.md) lists what it contains and every
check. The freeze is over: the menu stack (#384 to #388), #371 and #392 are next. The October 15 version of this section
is in [the October 15 snapshot](status/GOAL_LOOP_2026-10-15.md).

**Merged on October 17:** the Android port as an experimental own-disc build (LiquidAzir, #93, with the app-only
check in #372); Wayland presenting with mailbox (jslay88, #370); opt-in 4:3 Picto Box photos under widescreen
(pdale-boop, #338, with RecompCore#37); the in-app Wind Waker HD importer (#315); jslay88's benchmark tour (#360),
guest-only CPU comparison in the native tests (#359) and a source-compiled fmod, not hooked (#364); cforain's
runtime architecture docs (#337); the softer HD look (#321) and its sea fixes (#379, RecompCore#41); Direct3D 11 for
older Windows GPUs (#377); AppImage builds that pass their own ABI check (#374); the HD import script's sort order
(pdale-boop, #375); natives rounds 7, 5 and 4 behind their switches, off by default (jslay88 and Elliott Tate, #361,
#381 for #365 and #366); a Docker cache note and no secrets warning in the Linux container build (#382); and
[a record of what players asked on Discord](status/COMMUNITY_2026-10-17.md) (#378). The September records and the old ledgers moved to
[archive/status](archive/status).

| Platform | Download today | New in `main` | Still to do |
| --- | --- | --- | --- |
| Windows | 0.7.0 zip, built on Chris's PC | Behaviour timing on by default (#335); the Mac and Linux menu layout, controller navigation and hotkeys (#326, #329, #330, #332); the HD importer (#315); opt-in Picto Box crop (#338); Direct3D 11 for GPUs without Direct3D 12 (#377) | The checks in [WINDOWS_TASKS.md](WINDOWS_TASKS.md#1-in-main-for-the-next-windows-build), including an HD import, then the release build |
| Linux | None: build from source ([LINUX.md](LINUX.md)). 0.7.0's notes say an AppImage comes with 0.8.0 | The same menus, Restart now and safe mode; portable mode; the folder picker; behaviour timing; a resized window keeps drawing (#353); mailbox on Wayland (#370); the HD importer (#315); AppImage builds that pass their ABI check (#374) | An AppImage built from a contributor's own disc; Wayland on GNOME and the Deck's gamescope |
| Mac | None: build from source | The same menus and hotkeys; Mac builds with Mono or Homebrew SQLite installed (#367); the HD importer (#315) | The menu with a controller; #368's one desktop menu, played on the Mac; whether the prepared build becomes the default |
| iPhone, iPad | 0.7.0 app, with PadMint | Mods › Wind Waker HD textures (#314) and importing them through Files (#315); the D-pad in menus and gyro aim | A tap test of #316 on the iPad; an HD import on an iPhone or iPad |
| Android | None: build it yourself, experimental ([ANDROID.md](ANDROID.md)) | The port (#93): Outset at full speed on the Fold 7 | The HD import (#347); other phones; a Bluetooth controller with the D-pad option; #238 |

**The Wind Waker HD look** is in `main`, off by default, on every build; F2 switches it on the desktop
([WWHD_LOOK.md](WWHD_LOOK.md)). The softer default is in (#321), and the square cloud shadows and banding on the sea are fixed
(#379, RecompCore#41). pdale-boop's draft #371 (RecompCore#40) stops the
look and the clouds flickering with Smooth Motion above 60 FPS (#136).

**HD textures:** Mods › Wind Waker HD textures › Import from Wind Waker HD… makes a pack from your own HD dump or
WUD/WUX image on Windows, Mac, Linux, iPhone and iPad (#315). Full imports pass on the Mac and on Linux with
pdale-boop's own dump (3,546 textures); generated images pass on an Android emulator and the iOS simulator. Still
needed: a Windows import, an authentic WUD/WUX image, and an iPhone or iPad import.

**Speed:** behaviour timing is the Windows and Linux default (#335). The benchmark tour exists now (#360) and gets
its first run on a disc with the next candidate. Elliott's natives round 7 is in `main` behind `--native-entries` (#361,
jslay88), its replay generated at build time from the player's own translation; rounds 5 and 4 followed behind
`--native-gx` and `--native-set4` (#381). None is hooked until it is certified against a BlueWake module.

### Waiting on Chris

- **#342 / RecompCore#39** (the first Picto Box photo with cold shaders): after Windows and Linux play checks; #342
  also needs its conflicts with `main` resolved.
- **Apple defaults:** whether the prepared build and behaviour timing become the Mac and iOS default.
- **0.8.0:** publishing, once the checks in [RELEASE_0.8.0.md](status/RELEASE_0.8.0.md#checks-before-publishing) pass.

### Waiting on contributors

- **jslay88, Elliott Tate:** hashes from a BlueWake module, so the natives can be hooked one set at a time.
- **pdale-boop:** #368 in five pull requests (Chris builds each on the Mac); #371 rebased on `bluewake-next`;
  a Windows HD import.
- **LiquidAzir:** a Bluetooth controller with the D-pad option; other phones are welcome from anyone.
- **cforain, jkoehler11:** the AppImage from their own disc at the candidate.

### Next, in order

1. #347 (Android HD import): CI on `main` with #315, then its Android phone and lifecycle checks.
2. #368 in parts, each built and played on the Mac before it merges.
3. The 0.8.0 candidate ([RELEASE_0.8.0.md](status/RELEASE_0.8.0.md)): the Windows build on Chris's PC ([WINDOWS_BUILD_0.8.0.md](status/WINDOWS_BUILD_0.8.0.md)), a Linux AppImage
   from a contributor's own disc (after #374), the app-only IPA and PadMint's `player-check.sh`, the 468-place sweep
   and the benchmark tour against 0.7.0. While its release build runs on someone's PC, merge only docs into `main`.
4. Speed, horizon 2: the tour's first run; certifying and hooking the natives one set at a time; then profile
   what is left, one measured change per pull request.

## The plan from October 12 (Chris's decisions)

People compare BlueWake with the Wind Waker HD recomp (ZeldaWWHDRecomp), which ships ready-made builds for Windows,
Mac, Linux and Android without game code and builds the game on first start in about two minutes. Players want that
quality from BlueWake on every platform. Chris decided:

- **Speed on by default** (#277): lean memory and lean deadlines in the Windows and Linux builders, the lazy overlap
  watch in the app. Done.
- **A game-behaviour timing mode** (#290), measured for speed and for whether audio and cutscenes hold. Done: the
  Windows and Linux default since #335; Mac and iOS opt-in.
- **The HD look, rebuilt by us,** cheapest first, each an option with the same menu on every platform. Done in `main`
  (#284, #287, #306), off by default; softer shading in #321. Elliott Tate's October 3 video showed these effects;
  none of his code for them is in a public branch.
- **HD textures made in the app on every build:** #315, with Android in #347.
- **0.8.0 waits for the HD look,** with the Linux AppImage and the speed defaults.

## Later: Elliott's remaining speed work, then the decompilation

One pull request per row, measured on the benchmark (headless, unpaced, from save states), each result in
[PERFORMANCE.md](PERFORMANCE.md#results). The rows' history is in the
[October 15 snapshot](status/GOAL_LOOP_2026-10-15.md#after-070-catch-up-with-elliott-then-the-decompilation).

| Step | Notes |
| --- | --- |
| Natives rounds 4, 5 and 7, with Elliott's comparison tests | Round 7 in `main` behind `--native-entries` (#361): four leaves certified on a lean BlueWake module, the replay generated at build time. Rounds 5 and 4 in #365 and #366. #179: the tests compare guest state only (#359) |
| Elliott's upload and vertex changes (RecompCore `6f52a68` to `400728a`) | Merged by hand with patch 0157; dungeon maps, HD packs and lava colours checked before and after |
| A native compiled from the decompilation's source (`__ieee754_fmod`) | In `main`, not hooked (#364); the yes or no against round 7's replayed fmod comes with #361 |
| The host's per-call checks as one flag the module reads inline | After #242; measure on ARM |
| Draw fusion on the Mac | Forest Haven played on Linux and on the Mac first |
| Horizon 2, for 0.8 and after | Cycles charged per block, natives for the loops that cross chunks most, cached display lists, the graphics thread on four-core CPUs |

## On the first of every month

Add a row to PERFORMANCE.md's decompilation table from [decomp.dev](https://decomp.dev/zeldaret/tww). Profile the
benchmark, name the hot functions with the decompilation's `symbols.txt`, list the ones matched since last month,
and pick the next natives from them: replayed from the translation where exactness matters, compiled from source
where step 6 showed it pays. When the actor modules pass about 90% of code matched, revisit porting scenes from
source.
