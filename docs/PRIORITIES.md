# BlueWake priorities

The ranked backlog. What is being done on which day is in [GOAL_LOOP.md](GOAL_LOOP.md); this page says what comes
next and why, with links to the evidence. Owner: Chris. Updated October 15, 2026. The full October 15 table, with
every measurement, is kept in [status/PRIORITIES_2026-10-15.md](status/PRIORITIES_2026-10-15.md); the October 8
version is [status/PRIORITIES_2026-10-08.md](status/PRIORITIES_2026-10-08.md).

## How it's ordered

1. **Bugs that break the game first:** a crash, a soft lock, a controller that can't play, missing sound or drawing,
   or a regression in the current release.
2. **Performance next:** low and uneven frame rates are the loudest complaint, on every platform.
3. **Everything else after:** narrower bugs, then features, then new platforms.

Within a tier, what reaches the most players with the clearest fix goes first. A new crash or data-loss report goes
straight to the top. Keep the states apart: *suspected*, *cause found*, *fixed in main*, *shipped*, *confirmed*. Close
an issue only when its reporter confirms, or it is a clear duplicate.

## The list

| # | What | Issues | Where it stands | Next |
| --- | --- | --- | --- | --- |
| 1 | **Speed on slower PCs, the Steam Deck and phones** (the game thread) | [#59](https://github.com/chrissotraidis/bluewake/issues/59), [#137](https://github.com/chrissotraidis/bluewake/issues/137), [#159](https://github.com/chrissotraidis/bluewake/issues/159), [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#215](https://github.com/chrissotraidis/bluewake/issues/215), [#246](https://github.com/chrissotraidis/bluewake/issues/246) | Lean memory, lean deadlines and the lazy overlap watch are defaults (#277). Behaviour timing is the Windows and Linux default (#335): 7 to 11% faster on Intel desktops, 9% on the Deck, Outset at full speed on the Fold 7. Mac and iOS keep it opt-in ([results](PERFORMANCE.md#results)) | Profile what is left before the next change (row 9); the Apple default (row 6) |
| 2 | **The Wind Waker HD look and HD textures, the same on every build** | [#57](https://github.com/chrissotraidis/bluewake/issues/57), [#315](https://github.com/chrissotraidis/bluewake/pull/315), [#336](https://github.com/chrissotraidis/bluewake/issues/336) | The look is in `main`, off by default, F2 on the desktop ([WWHD_LOOK.md](WWHD_LOOK.md)); a softer version is draft #321. The in-app importer is draft #315: full imports pass on the Mac, on Linux (pdale-boop's own dump) and on an Android emulator (#347); the iOS menu passes with generated files on the simulator | #315: a Windows import, an authentic WUD/WUX image and an iPhone or iPad import, then merge; #347 after #93. #336: the renderer's loaded-texture count. Chris's call on #321 |
| 3 | **Clouds and waves flicker** at 60 and 120 FPS with Smooth Motion (NVIDIA, Intel Arc, AMD) | [#136](https://github.com/chrissotraidis/bluewake/issues/136) | Smooth Motion's in-between frames place the sky wrongly in fast turns; draw fusion is ruled out. Not reproduced on the Mac | Trace the sky draws' blend in `frame_interp.cpp` (RecompCore) on a machine that shows it. Until then, Smooth Motion at 60 or off |
| 4 | **Linux crashed when the window was resized** | [#325](https://github.com/chrissotraidis/bluewake/issues/325) | [RecompCore#38](https://github.com/chrissotraidis/RecompCore/pull/38) (pdale-boop) fixes it; pinned in `main` by #353 (RecompCore `f647d03`). Checked on Arch with X11 and NVIDIA, also with F1 open, and on the Mac (Metal) with zoom and full screen; Windows and Linux CI pass | The reporter to confirm on a build from `main`. Windows (Direct3D 12) resize and fullscreen checked by pdale-boop; resizing with F1 open is checked on Linux only, and Wayland is unchecked |
| 5 | **Linux as a download** | [#56](https://github.com/chrissotraidis/bluewake/issues/56), [#215](https://github.com/chrissotraidis/bluewake/issues/215) | Builds and plays from source; the AppImage tooling passes CI. 0.7.0's notes say the AppImage comes with 0.8.0 | An AppImage from a contributor's own disc at the next candidate (cforain, jkoehler11), audited before upload |
| 6 | **Mac, iPhone and iPad: speed and the menus** | [#104](https://github.com/chrissotraidis/bluewake/issues/104), [#316](https://github.com/chrissotraidis/bluewake/pull/316) | Tiered builds (#252). The PC builds' preparations and behaviour timing are opt-in on Apple; on the M2 iPad Pro the prepared build took Outset's sea from 81% to 54% game-thread busy | Chris: the Apple default. #316 needs a tap test on the iPad |
| 7 | **Android at the iPhone app's standard** | [#75](https://github.com/chrissotraidis/bluewake/issues/75), [#93](https://github.com/chrissotraidis/bluewake/pull/93), [#238](https://github.com/chrissotraidis/bluewake/issues/238) | LiquidAzir's own-disc port plays on a Fold 7, Outset at full speed with behaviour timing. Not in `main` | #93: LiquidAzir's `--no-tiered` check and CI, then merge as "build it yourself, experimental". Bluetooth with the D-pad option, other phones, and #238 (screen recording loses the GPU) |
| 8 | **The crash sweep, the benchmark tour and the Wind Temple's R04 and R08** | [#318](https://github.com/chrissotraidis/bluewake/issues/318) | The stage select and `BLUEWAKE_WARP` are in `main`. The loose-REL switch (#331, off by default) makes R04 and R08 load; save states wait for it (#350) ([evidence](status/WIND_TEMPLE_2026-10-15.md)) | F5 and F8 during a real wait, then decide the default; the 468-place sweep on the next candidate |
| 9 | **Speed, horizon 2:** natives, cached display lists, graphics work on four-core CPUs | [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#179](https://github.com/chrissotraidis/bluewake/issues/179) | The certified natives matched 0 of 15 on October 11 | Fix the host-pointer comparisons and re-record the hashes (#179); one measured change per pull request |
| 10 | **Releases without game code** ([DIRECTION.md](DIRECTION.md#1-no-game-code-in-any-release)) | none | Not started | After the speed work settles the build options |
| 11 | **Control and mod requests** | [#244](https://github.com/chrissotraidis/bluewake/issues/244), [#245](https://github.com/chrissotraidis/bluewake/issues/245), [#188](https://github.com/chrissotraidis/bluewake/issues/188), [#70](https://github.com/chrissotraidis/bluewake/issues/70), [#108](https://github.com/chrissotraidis/bluewake/issues/108), [#152](https://github.com/chrissotraidis/bluewake/issues/152), [#259](https://github.com/chrissotraidis/bluewake/issues/259) | In `main`: Swap L and R (#244), D-pad menus (#245), gyro aim (#188), 21:9 (#70), F2 for the HD look (#108), Better Wind Waker, Linux portable mode | Reporters to try the next build; custom file-replacement mods (#152) and the over-the-shoulder camera (#259) after the performance work |
| 12 | Older single reports: a Forsaken Fortress soft lock, the pirate flag's missing texture | [#76](https://github.com/chrissotraidis/bluewake/issues/76), [#69](https://github.com/chrissotraidis/bluewake/issues/69) | Unverified, needs info | Reproduce from a copied save |
| 13 | iPhone and iPad builds from Windows through PadMint | [#100](https://github.com/chrissotraidis/bluewake/pull/100), [#46](https://github.com/chrissotraidis/bluewake/pull/46) | Parked drafts | A maintainer decision |

Not planned now: the European and Japanese discs ([#60](https://github.com/chrissotraidis/bluewake/issues/60)), Intel Macs ([#48](https://github.com/chrissotraidis/bluewake/issues/48)), Switch ([#62](https://github.com/chrissotraidis/bluewake/issues/62)).
The Wii U-style interface ([#57](https://github.com/chrissotraidis/bluewake/issues/57)) is part of the HD look (row 2). CPUs without AVX2 get a clear message and no build ([#77](https://github.com/chrissotraidis/bluewake/issues/77)).

## Fixed in `main`, for the next build

The reporters confirm on the next build: the Picto Box credits the right subject (#267; #333 is confirmed and
closed), each island's own sea chart (#263), the D-pad in menus (#245), 21:9 (#70) and F2 (#108). Shipped in 0.7.0
and waiting for the reporter: #138 (the stick at launch), #190 (rumble), #64 (portable mode); earlier, #154, #55,
#58, #66, #73. Confirmed and closed: #74, #155, #191, #313, #308. Closed as answered: #212 and #322 (other languages
are #60). Closed as a duplicate of #59: #255.

## Waiting on someone else

| Who | What | Where |
| --- | --- | --- |
| LiquidAzir | Reject `--app-only --no-tiered` for a tiered module, with a fixture; Bluetooth with the D-pad option | #93 |
| pdale-boop | A Windows import with the in-app importer, and a WUD/WUX import if he has one (asked October 15) | #315 |
| cforain, jkoehler11 | An AppImage from their own disc at the next release candidate, on the agreed Clang 22 path | #56, #215, #254 |
| cforain | Clang 18 against `--clang22` at one benchmark spot; runtime docs still being revised | #254, #337 |
| elbahijeadam | A log from Outset and the open sea | #246 |
| jay080021 | Their CPU and a bird-scene log; whether the pirate flag has its texture | #59, #69 |
| A physical iPad | A tap through the ⋯ menu's switches with #316 | #316 |
| Chris | #321 (on his iPad); #342 with RecompCore#39; the Apple default; when to freeze 0.8.0 | [GOAL_LOOP.md](GOAL_LOOP.md#waiting-on-chris) |

## Contributors' pull requests

Review within two days ([DIRECTION.md](DIRECTION.md)). Since 0.7.0, `main` has 21 pull requests from pdale-boop
(speed, the stage select, the same menus on every build, the Wind Temple switch, widescreen gallery photos), four from
cforain (the Linux build container and build docs), two from jkoehler11 (the Linux folder picker and texture status),
jslay88's Picto Box fix and Mrlinkwii's CI update. The merge history is in the
[October 15 snapshot](status/PRIORITIES_2026-10-15.md#contributors-pull-requests).

Open now:

- **#93** (LiquidAzir, Android): one option check left, then CI and merge as experimental.
- **#338** (pdale-boop, opt-in 4:3 Picto Box photos under widescreen): needs RecompCore#37 and a runtime decision;
  its CI fails until then, and conflicts with `main` need resolving before integration.
- **#337** (cforain, runtime docs): still draft; revised October 15 after feedback on platform entry paths and
  duplication. Review the latest revision when the author marks it ready.
- **RecompCore#37** (pdale-boop, the crop API): goes with #338.
