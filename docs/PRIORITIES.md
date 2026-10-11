# BlueWake priorities

The ranked backlog and the evidence behind it. What is being done on which day is in [GOAL_LOOP.md](GOAL_LOOP.md);
this page says what comes next and why. Owner: Chris. Updated October 12, 2026. The October 8 version, with the
0.6.0 tables and full evidence, is kept in [status/PRIORITIES_2026-10-08.md](status/PRIORITIES_2026-10-08.md).

## How it's ordered

1. **Bugs that break the game first:** a crash, a soft lock, a controller that can't play, missing sound or drawing,
   or a regression in the current release.
2. **Performance next:** low and uneven frame rates are the loudest complaint, on every platform.
3. **Everything else after:** narrower bugs, then features, then new platforms.

Within a tier, what reaches the most players with the clearest fix goes first. A new crash or data-loss report goes
straight to the top. Keep the states apart: *suspected*, *cause found*, *fixed in main*, *shipped*, *confirmed*. Close
an issue only when its reporter confirms, or it is a clear duplicate.

## The list

| # | What | Issues | State | Next |
| --- | --- | --- | --- | --- |
| 1 | **Speed: the host's per-call work.** LiquidAzir's profile on the Fold 7 ([#93](https://github.com/chrissotraidis/bluewake/pull/93)): per retrace the host and runtime cost 40 M instructions in BlueWake against 7 M in Wind Waker Recomp's build; `host_direct_can_skip` alone is 23 M. The game thread is the limit on every slow CPU, from a 2-core laptop to the Steam Deck | [#59](https://github.com/chrissotraidis/bluewake/issues/59), [#137](https://github.com/chrissotraidis/bluewake/issues/137), [#159](https://github.com/chrissotraidis/bluewake/issues/159), [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#215](https://github.com/chrissotraidis/bluewake/issues/215), [#246](https://github.com/chrissotraidis/bluewake/issues/246), [#93](https://github.com/chrissotraidis/bluewake/pull/93) | 0.7.0 shipped lean blocks, hidden symbols (Linux) and draw fusion (Windows). [#242](https://github.com/chrissotraidis/bluewake/pull/242) (pdale-boop) merged October 11: the per-block checks do less, 7% fewer instructions, 5 to 8% faster on slow x86 CPUs, identical game state. On this Mac (October 12, [record](status/SPEED_PROFILE_2026-10-11.md)): `--lean-memory` 5.5 to 6% faster, and `--lean-deadlines` ([#264](https://github.com/chrissotraidis/bluewake/pull/264)) 3.7 to 4.9% on top, both with the same game state and off by default. The host's per-call checks answered inside the module agreed with the host on every call but were no faster, so that idea is dropped | The phone, the Deck and an x86 PC remeasured on `main` with both options; Chris decides the defaults. On Apple, the training link for the larger module ([GOAL_LOOP.md](GOAL_LOOP.md) S2) |
| 2 | Clouds, distant waves and fog flicker at 60 and 120 FPS on NVIDIA, new in 0.5.0 | [#136](https://github.com/chrissotraidis/bluewake/issues/136) | Suspected. Clean at 30 FPS, so in Smooth Motion's in-between frames. Ruled out by the reporters on 0.7.0: patches 0152 and 0155 (their switches), the ubershader, the HD texture pack and G-Sync | See whether it shows on Chris's PC (AMD graphics) at 120; then the capture steps in [WINDOWS_TASKS.md](WINDOWS_TASKS.md) on an NVIDIA PC, compared with 0.4.0 |
| 2a | The sea chart shows the first island map you opened for every island until a restart (0.7.0, HD textures on or off) | [#263](https://github.com/chrissotraidis/bluewake/issues/263) | Suspected: the texture cache rechecks an image over 4 KB only when something marks its memory changed, and the game unpacks each island's map into the same place with its own code, which nothing marks (RecompCore patches 0042 and 0080; 0080 rechecks only images up to 4 KB) | pdale-boop: the opt-in cache-flush fallback (`BLUEWAKE_CACHE_FLUSH_FALLBACK=1`, #13) fixed it for him; it turns the game's cache flushes into the texture cache's change marks. The reporter (Windows, HD pack) is trying it. On this Mac it costs nothing measurable on the game thread (Outset 22.41 s against 22.47 s, the cavern 14.19 s against 14.24 s, identical checkpoints; headless, so the GX worker's extra hashing isn't in it) | If it fixes it for the reporter, Chris decides whether it goes on by default, which also settles #13 |
| 3 | **Linux as a download.** 0.7.0 builds and plays from source (two laptops, a Steam Deck, a Gentoo desktop) | [#56](https://github.com/chrissotraidis/bluewake/issues/56), [#203](https://github.com/chrissotraidis/bluewake/issues/203), [#215](https://github.com/chrissotraidis/bluewake/issues/215) | The AppImage still needs a contributor's own-disc build. Draw fusion on for Linux, merged in [#247](https://github.com/chrissotraidis/bluewake/pull/247) (jkoehler11's Deck run: a tenth of the draws, the GX worker 45% to 24%, nothing missing) | cforain's shared build container ([#254](https://github.com/chrissotraidis/bluewake/pull/254)) once its image has clang 22; the AppImage built with it from the next release commit, on a Linux PC; the release check |
| 4 | **Android at the iPhone app's standard.** Builds from your own disc and plays; the ⋯ menu, touch, controllers, saves, texture packs and folding are all checked on a Fold 7 | [#75](https://github.com/chrissotraidis/bluewake/issues/75), [#93](https://github.com/chrissotraidis/bluewake/pull/93), [#238](https://github.com/chrissotraidis/bluewake/issues/238) | Up to date with 0.7.0. Speed is the only gate left: Outset at 67 to 80% when cool (172 M a retrace against 110 M), against "holds 30" | LiquidAzir merges `main` ([#242](https://github.com/chrissotraidis/bluewake/pull/242)), turns hidden symbols off for Android (9 M more on ARM64), measures the pier; merged as "build it yourself, experimental" when Outset holds 30. [#238](https://github.com/chrissotraidis/bluewake/issues/238): the screen recorder loses the Vulkan device |
| 5 | **The crash sweep and benchmark tour:** warp to all 468 places on every release candidate; one command that measures a build at fixed spots | none | The stage select and `BLUEWAKE_WARP` are in `main` ([#210](https://github.com/chrissotraidis/bluewake/pull/210), merged October 11, no cost when off) | The sweep on the next candidate; `bench_tour.py` on `BLUEWAKE_WARP` |
| 6 | **Mac, iPhone and iPad builds.** They compile the raw translated source, with none of the PC builds' preparations (direct calls, natives, prepaid and lean block copies): `--combined-optimizations` is opt-in on the Mac and refused for iOS. An iPad Pro (M2) still holds 30 (game thread 81% busy at Outset, 52% in Dragon Roost Cavern), so what's at stake is older devices (the build targets the A13) and build time | [#104](https://github.com/chrissotraidis/bluewake/issues/104) | Tiered compile merged ([#252](https://github.com/chrissotraidis/bluewake/pull/252)): 61 minutes instead of 79 on an M3 Max, the same speed on the iPad | An iPad build with the combined preparations and lean blocks, measured at the same two places with copied cards and against #252's compile time; on by default if it plays the same |
| 7 | **Speed, horizon 2:** lighter timing, natives that remove round trips (round 4, 5 and 7, re-certified on a lean build), cached display lists, the graphics thread on four-core CPUs | [#86](https://github.com/chrissotraidis/bluewake/issues/86), [#179](https://github.com/chrissotraidis/bluewake/issues/179) | Planned | 0.8 |
| 8 | **Releases without game code:** a maintainer-trained profile, then a first-launch build with a progress screen ([DIRECTION.md](DIRECTION.md#1-no-game-code-in-any-release)) | none | Not started | After the speed work settles the build options |
| 9 | Older single reports: a Forsaken Fortress soft lock, the pirate flag's missing texture, the Pictobox's stale photo fix on by default | [#76](https://github.com/chrissotraidis/bluewake/issues/76), [#69](https://github.com/chrissotraidis/bluewake/issues/69), #13 | Unverified, needs info, opt-in | Reproduce from a copied save; a physical iPad for #13 |
| 10 | **Features,** in order: Swap L and R ([#248](https://github.com/chrissotraidis/bluewake/pull/248), merged), the D-pad in the item screens, gyro aiming, a controller picker, HD textures from the menu, Elliott's HD renderer work, ultrawide, Wind Waker HD options, a mod folder, a graphics hotkey | [#244](https://github.com/chrissotraidis/bluewake/issues/244), [#245](https://github.com/chrissotraidis/bluewake/issues/245), [#188](https://github.com/chrissotraidis/bluewake/issues/188), [#155](https://github.com/chrissotraidis/bluewake/issues/155), [#70](https://github.com/chrissotraidis/bluewake/issues/70), [#152](https://github.com/chrissotraidis/bluewake/issues/152), [#108](https://github.com/chrissotraidis/bluewake/issues/108) | Requests; [#244](https://github.com/chrissotraidis/bluewake/issues/244) is in `main` | After the speed work, except small ones like [#244](https://github.com/chrissotraidis/bluewake/issues/244) |
| 11 | iPhone and iPad builds from Windows through PadMint | [#100](https://github.com/chrissotraidis/bluewake/pull/100), [#46](https://github.com/chrissotraidis/bluewake/pull/46) | Parked drafts | A maintainer decision |

Not planned now: the European and Japanese discs ([#60](https://github.com/chrissotraidis/bluewake/issues/60)), Intel Macs ([#48](https://github.com/chrissotraidis/bluewake/issues/48)), Switch ([#62](https://github.com/chrissotraidis/bluewake/issues/62)), the Wii U-style
interface ([#57](https://github.com/chrissotraidis/bluewake/issues/57)). CPUs without AVX2 get a clear message and no build ([#77](https://github.com/chrissotraidis/bluewake/issues/77)).

## Waiting on someone else

| Who | What | Where |
| --- | --- | --- |
| LiquidAzir | Android on `main` with #242 and hidden symbols off, measured at the pier | #93 |
| cforain, jkoehler11 | The AppImage from their own disc; a Deck before and after on `main` | #56, #215 |
| cforain | Clang 18 against `--clang22` at one benchmark spot, to choose the container's default | #254 |
| elbahijeadam | A log from Outset and the open sea (all earlier logs were the title screen; there 0.7.0 went from 26 to 34% speed to 50%) | #246 |
| Shockishock | The cache-flush fallback on Windows, at the sea chart and Charts | #263 |
| TheTimeKeeper95-clock | Platform, widescreen on or off, and a log for the refused Lenzo photo | #267 |
| A Windows PC with NVIDIA graphics | The flicker capture (WINDOWS_TASKS.md section 4) | #136 |
| jay080021 | Their CPU and a bird-scene log; whether the pirate flag has its texture | #59, #69 |
| Elliott | Where the HD renderer work (lighting, shadows, the GameCube/HD switch) lives | Direct |
| A physical iPad | The Pictobox fallback, before it is on by default | #13 |

Shipped and waiting for the reporter to confirm: in 0.7.0, #138 (the stick at launch), #190 (rumble), #191 (Pictobox
stick), #64 (portable mode); earlier, #154, #55, #58, #66, #73. Confirmed and closed: #74 (October 10), #155 (the
Mayflash adapter, October 11). Closed as answered: #212 (other languages are #60). Closed as a duplicate of #59: #255.

## Contributors' pull requests

Review within two days ([DIRECTION.md](DIRECTION.md)). Merged October 11: #242, #210, #243 and #250 (pdale-boop), with #247 and #248. Merged later that day: #252. Merged October 12: #254 (cforain's shared Linux build container), #266 (stage select: saving off once used, #251) and #268 (RELR on Linux), with #264 and #270. Open: Android
(#93, LiquidAzir, waiting on speed).
