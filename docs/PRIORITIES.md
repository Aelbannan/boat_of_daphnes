# BlueWake priorities

The ranked backlog. What is being done on which day is in [GOAL_LOOP.md](GOAL_LOOP.md); this page says what comes
next and why, with links to the evidence. Owner: Chris. Updated October 17, 2026. The full October 15 table, with
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
| 1 | **Speed on slower PCs, laptops, the Steam Deck and phones** (the game thread) | [#59](https://github.com/chrissotraidis/bluewake/issues/59), [#137](https://github.com/chrissotraidis/bluewake/issues/137), [#159](https://github.com/chrissotraidis/bluewake/issues/159), [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#215](https://github.com/chrissotraidis/bluewake/issues/215), [#246](https://github.com/chrissotraidis/bluewake/issues/246), [#369](https://github.com/chrissotraidis/bluewake/issues/369) | Lean memory, lean deadlines and the lazy overlap watch are defaults (#277). Behaviour timing is the Windows and Linux default (#335): 7 to 11% faster on Intel desktops, 9% on the Deck, Outset at full speed on the Fold 7. Mac and iOS keep it opt-in ([results](PERFORMANCE.md#results)). A Deck build of `main` holds 22 to 27 at the #215 spot (irregularthanis, October 16) | The benchmark tour's first run (row 9); the Apple default (row 6) |
| 2 | **The Wind Waker HD look and HD textures, the same on every build** | [#57](https://github.com/chrissotraidis/bluewake/issues/57), [#336](https://github.com/chrissotraidis/bluewake/issues/336) | The look is in `main`, off by default, F2 on the desktop ([WWHD_LOOK.md](WWHD_LOOK.md)); a softer version is draft #321. The in-app importer is in `main` (#315): full imports on the Mac and on Linux (pdale-boop's dump), generated images on an Android emulator and the iOS simulator. Android's menu for it is draft #347 | A Windows import, an authentic WUD/WUX image and an iPhone or iPad import; #347 after its phone checks. #336: the renderer's loaded-texture count. Chris's call on #321 |
| 3 | **Clouds and waves flicker** with Smooth Motion above 60 FPS | [#136](https://github.com/chrissotraidis/bluewake/issues/136) | pdale-boop's draft [#371](https://github.com/chrissotraidis/bluewake/pull/371) (RecompCore#40) gives every in-between frame the HD look and pairs each cloud and wave crest with itself; jslay88 saw the look hold on KWin at 120 Hz. A few single clouds still flicker | #371 rebased on `bluewake-next` (it predates RecompCore#37), then tests on NVIDIA, Intel Arc and AMD. Until then, Smooth Motion at 60 or off |
| 4 | **Linux: a resized window crashed** | [#325](https://github.com/chrissotraidis/bluewake/issues/325) | Fixed in `main` (RecompCore#38, #353), checked on Arch/X11/NVIDIA, the Mac and Windows | The reporter on a `main` build; Wayland |
| 5 | **Linux as a download** | [#56](https://github.com/chrissotraidis/bluewake/issues/56), [#215](https://github.com/chrissotraidis/bluewake/issues/215) | Builds and plays from source; the AppImage tooling passes CI. Mailbox present on Wayland (#370) keeps KWin 6.8 at full speed. 0.7.0's notes say the AppImage comes with 0.8.0 | An AppImage from a contributor's own disc at the next candidate (cforain, jkoehler11), audited before upload; Wayland on GNOME and gamescope |
| 6 | **Mac, iPhone and iPad: speed and the menus** | [#104](https://github.com/chrissotraidis/bluewake/issues/104) | Tiered builds (#252). The PC builds' preparations and behaviour timing are opt-in on Apple; on the M2 iPad Pro the prepared build took Outset's sea from 81% to 54% game-thread busy. Mac builds with Mono or Homebrew SQLite work (#367) | Chris: the Apple default. [#316](https://github.com/chrissotraidis/bluewake/pull/316) needs a tap test on the iPad. #368's desktop menu built and played on the Mac |
| 7 | **Android at the iPhone app's standard** | [#75](https://github.com/chrissotraidis/bluewake/issues/75), [#238](https://github.com/chrissotraidis/bluewake/issues/238) | In `main` as an experimental own-disc build (LiquidAzir, #93, October 17), with `--app-only` refusing a module built differently (#372). Outset at full speed on the Fold 7 | The HD import ([#347](https://github.com/chrissotraidis/bluewake/pull/347)); a Bluetooth controller with the D-pad option; other phones; #238 (screen recording loses the GPU) |
| 8 | **The crash sweep and the Wind Temple's R04 and R08** | [#318](https://github.com/chrissotraidis/bluewake/issues/318) | The stage select and `BLUEWAKE_WARP` are in `main`. The loose-REL switch (#331, off by default) makes R04 and R08 load; save states wait for it (#350) ([evidence](status/WIND_TEMPLE_2026-10-15.md)) | F5 and F8 during a real wait, then decide the default; the 468-place sweep on the next candidate |
| 9 | **Speed, horizon 2:** the benchmark, natives, cached display lists, graphics work on four-core CPUs | [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#179](https://github.com/chrissotraidis/bluewake/issues/179) | The benchmark tour is in `main` (#360), not yet run on a disc. The native tests compare guest state only (#359); real guest differences remain, so the 15 hashes stay. Natives rounds 4, 5 and 7 ([#366](https://github.com/chrissotraidis/bluewake/pull/366), [#365](https://github.com/chrissotraidis/bluewake/pull/365), [#361](https://github.com/chrissotraidis/bluewake/pull/361)) are on hold until their generated code is made at build time. A source-compiled fmod is in, not hooked (#364) | The tour's first run against 0.7.0; the natives generated at build time; one measured change per pull request |
| 10 | **Releases without game code** ([DIRECTION.md](DIRECTION.md#1-no-game-code-in-any-release)) | none | Not started | After the speed work settles the build options |
| 11 | **Control and mod requests** | [#244](https://github.com/chrissotraidis/bluewake/issues/244), [#245](https://github.com/chrissotraidis/bluewake/issues/245), [#188](https://github.com/chrissotraidis/bluewake/issues/188), [#70](https://github.com/chrissotraidis/bluewake/issues/70), [#108](https://github.com/chrissotraidis/bluewake/issues/108), [#152](https://github.com/chrissotraidis/bluewake/issues/152), [#259](https://github.com/chrissotraidis/bluewake/issues/259), [#357](https://github.com/chrissotraidis/bluewake/issues/357), [#358](https://github.com/chrissotraidis/bluewake/issues/358) | In `main`: Swap L and R (#244), D-pad menus (#245), gyro aim (#188), 21:9 (#70), F2 for the HD look (#108), Better Wind Waker, Linux portable mode, opt-in 4:3 Picto Box photos (#338). One desktop menu for Mac, Linux and Windows is draft [#368](https://github.com/chrissotraidis/bluewake/pull/368) | Reporters to try the next build; save states on a controller (#357) after #368; keyboard prompts (#358) through texture packs for now; custom mods (#152) and the over-the-shoulder camera (#259) after the performance work |
| 12 | Older single reports: a Forsaken Fortress soft lock, the pirate flag's missing texture | [#76](https://github.com/chrissotraidis/bluewake/issues/76), [#69](https://github.com/chrissotraidis/bluewake/issues/69) | Unverified, needs info | Reproduce from a copied save |
| 13 | iPhone and iPad builds from Windows through PadMint | [#100](https://github.com/chrissotraidis/bluewake/pull/100), [#46](https://github.com/chrissotraidis/bluewake/pull/46) | Parked drafts | A maintainer decision |

Not planned now: the European and Japanese discs ([#60](https://github.com/chrissotraidis/bluewake/issues/60)), Intel Macs ([#48](https://github.com/chrissotraidis/bluewake/issues/48)), Switch ([#62](https://github.com/chrissotraidis/bluewake/issues/62)).
The Wii U-style interface ([#57](https://github.com/chrissotraidis/bluewake/issues/57)) is part of the HD look (row 2). CPUs without AVX2 get a clear message and no build ([#77](https://github.com/chrissotraidis/bluewake/issues/77)).

## Fixed in `main`, for the next build

The reporters confirm on the next build: the Picto Box credits the right subject (#267; #333 is confirmed and
closed), each island's own sea chart (#263), the D-pad in menus (#245), 21:9 (#70), F2 (#108) and the Linux resize
(#325). Shipped in 0.7.0 and waiting for the reporter: #138 (the stick at launch), #190 (rumble), #64 (portable mode);
earlier, #154, #55, #58, #66, #73. Confirmed and closed: #74, #155, #191, #313, #308. Closed as answered: #212 and #322
(other languages are #60). Closed as a duplicate of #59: #255.

## Waiting on someone else

| Who | What | Where |
| --- | --- | --- |
| jslay88 | The natives' `_gen.inc` files made at build time from the player's own translation, with Elliott credited | #361, #365, #366 |
| pdale-boop | #368 in five pull requests; #371 rebased on `bluewake-next`; a Windows HD import | #368, #371, #315 |
| LiquidAzir | A Bluetooth controller with the D-pad option | #93 |
| cforain, jkoehler11 | An AppImage from their own disc at the next release candidate, on the agreed Clang 22 path | #56, #215, #254 |
| cforain | Clang 18 against `--clang22` at one benchmark spot | #254 |
| ellapp-ok | A session log, the power plan, and the HD textures off | #369 |
| elbahijeadam | A log from Outset and the open sea | #246 |
| jay080021 | Their CPU and a bird-scene log; whether the pirate flag has its texture | #59, #69 |
| A physical iPad | A tap through the ⋯ menu's switches with #316 | #316 |
| Chris | #321 (on his iPad); #342 with RecompCore#39; the Apple default; when to freeze 0.8.0 | [GOAL_LOOP.md](GOAL_LOOP.md#waiting-on-chris) |

## Contributors' pull requests

Review within two days ([DIRECTION.md](DIRECTION.md)). Merged on October 17: LiquidAzir's Android port (#93),
jslay88's Wayland present (#370), benchmark tour (#360), native-test comparison (#359) and fmod shim (#364),
pdale-boop's Picto Box crop (#338, with RecompCore#37), and cforain's runtime docs (#337). The earlier merge history
is in the [October 15 snapshot](status/PRIORITIES_2026-10-15.md#contributors-pull-requests).

Open now:

- **#368** (pdale-boop, one desktop menu): agreed to split into five pull requests; Chris builds each on the Mac.
- **#371** (pdale-boop, Smooth Motion without the flicker, RecompCore#40): draft; needs rebasing on `bluewake-next`.
- **#361, #365, #366** (jslay88, natives rounds 7, 5 and 4): on hold until the generated code is made at build time.
