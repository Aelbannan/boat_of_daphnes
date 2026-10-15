# What we're doing

The dated plan, and the five-step loop every session follows. Owner: Chris. Updated October 15, 2026. The ranked
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

## Where things stand on October 15

**The latest download is 0.7.0** (October 10): the Windows zip, the iPhone and iPad app for PadMint, and the source.
Nothing has been released since. `main` has 86 merged pull requests on top of it: 55 change the app or its builds
and 31 are docs. pdale-boop wrote 21 of them; cforain, jkoehler11, jslay88 and Mrlinkwii contributed too. The
[next release's draft](status/RELEASE_NEXT_DRAFT_2026-10-11.md) lists the changes with their checks. 0.8.0 waits for
the HD work (Chris, October 12).

| Platform | Download today | New in `main` | Still to do |
| --- | --- | --- | --- |
| Windows | 0.7.0 zip, built on Chris's PC | Behaviour timing on by default (#335); the Mac and Linux menu layout, controller navigation and the same hotkeys (#326, #329, #330, #332) | The checks in [WINDOWS_TASKS.md](WINDOWS_TASKS.md#1-in-main-for-the-next-windows-build) on a Windows PC, then the release build |
| Linux | None: build from source ([LINUX.md](LINUX.md)). 0.7.0's notes say a ready-made AppImage comes with 0.8.0 | The same menus, Restart now and safe mode (#324, #327, #328); portable mode (#303); the folder picker (#344); behaviour timing (#335); resizing the window no longer crashes (RecompCore#38, pdale-boop, #353) | An AppImage built from a contributor's own disc; resize on Wayland |
| Mac | None: build from source | The same menus and hotkeys (#324, #326, #327) | The menu with a controller; whether the prepared build becomes the default |
| iPhone, iPad | 0.7.0 app, with PadMint | Mods › Wind Waker HD textures (#314); the D-pad in menus and gyro aim (#301, #304) | A tap test of #316 (menu switches keep the menu open) on the iPad; an HD import on hardware (#315) |
| Android | None: experimental own-disc builds from #93 | Not in `main` yet | #93: one builder check from LiquidAzir, then merge as "build it yourself, experimental"; HD import (#347); other phones; #238 |

**The Wind Waker HD look** (distant islands, ambient occlusion, sun and cloud shadows, edge and torch light, far
blur, bloom) is in `main`, off by default, on Windows, Mac, Linux, iPhone and iPad; F2 switches it on the desktop
([WWHD_LOOK.md](WWHD_LOOK.md)). After Chris's iPad feedback, a softer version is draft #321.

**HD textures:** a pack made with the import script installs from **Mods › Wind Waker HD textures** on Windows, Mac,
Linux, iPhone and iPad ([WWHD_TEXTURES.md](WWHD_TEXTURES.md)). The importer built into the app, from a decrypted HD
folder or a WUD/WUX image with its key files, is draft #315. Full imports pass on the Mac, on Linux (pdale-boop's
own dump, October 15) and on an Android emulator (#347). The iOS menu passes with generated files on the simulator,
which also found a stale "No pack installed yet" after an import, now fixed. Still needed: a Windows import, an
authentic WUD/WUX image, and checks on an iPhone, an iPad and an Android phone.

**Speed:** behaviour timing is the Windows and Linux default (#335): 7 to 11% faster on Intel desktops, 9% on the
Steam Deck, and Outset at full speed on LiquidAzir's Fold 7. Mac and iOS builds keep it opt-in
([PERFORMANCE.md](PERFORMANCE.md#results)).

**Fixed in `main`, for the next build:** the Picto Box credits the right subject (#267), each island shows its own
sea chart (#263), the Greatfish merchant stays on his raft (#313), save states wait for loose module loads (#350),
safe startup turns the HD look off (#343), and Picto Box gallery photos fit their frames under widescreen (#346).

### Waiting on Chris

- **#321:** whether the softer HD look is right; a build with it is going on Chris's iPad.
- **#342 / RecompCore#39** (the first Picto Box photo with cold shaders): after Windows and Linux play checks.
- **Apple defaults:** whether the prepared build and behaviour timing become the Mac and iOS default.
- **0.8.0:** when to freeze.

### Next, in order

1. #315: a Windows import (asked of pdale-boop on October 15) and the iOS fix checked again on the simulator; then
   merge. #347 follows #93.
2. #93: merge once LiquidAzir's `--no-tiered` check is in and CI passes.
3. The 0.8.0 candidate: the 468-place sweep, the Windows checks, a Linux AppImage and PadMint's `player-check.sh`.
   While its release build runs on someone's PC, merge only docs into `main`.
4. Speed, horizon 2: profile what is left, then one measured change per pull request.

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
| Natives rounds 4, 5 and 7, with Elliott's comparison tests | First make #179's certification pass on a lean build (0 of 15 on October 11) |
| Elliott's upload and vertex changes (RecompCore `6f52a68` to `400728a`) | Merged by hand with patch 0157; dungeon maps, HD packs and lava colours checked before and after |
| A native compiled from the decompilation's source (`__ieee754_fmod`) | A yes or no for doing more |
| The host's per-call checks as one flag the module reads inline | After #242; measure on ARM |
| Draw fusion on the Mac | Forest Haven played on Linux and on the Mac first |
| Horizon 2, for 0.8 and after | Cycles charged per block, natives for the loops that cross chunks most, cached display lists, the graphics thread on four-core CPUs |

## On the first of every month

Add a row to PERFORMANCE.md's decompilation table from [decomp.dev](https://decomp.dev/zeldaret/tww). Profile the
benchmark, name the hot functions with the decompilation's `symbols.txt`, list the ones matched since last month,
and pick the next natives from them: replayed from the translation where exactness matters, compiled from source
where step 6 showed it pays. When the actor modules pass about 90% of code matched, revisit porting scenes from
source.
