# What we're doing

The dated plan. Ask "what are we doing on October 11?" and the answer is that day's section below. Owner: Chris.
Updated October 13, 2026.

**The aim for the next versions:** BlueWake runs faster and steadier, the bugs players reported are fixed, Linux
becomes a download, and Android gets to the iPhone app's standard. Most of the speed is already written, by Elliott
Tate after October 3 and by contributors, so bringing it in, measured, comes first. The ranked backlog is
[PRIORITIES.md](PRIORITIES.md), the speed strategy and its runbook are [PERFORMANCE.md](PERFORMANCE.md), and the
standards everything is held to are in [DIRECTION.md](DIRECTION.md).

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
| 1. Lean what we have, and catch up with Elliott | 0.7.0 and the next two weeks | Lean block copies (default since today), draw fusion, his faster loads and natives rounds 4, 5 and 7, his upload changes, hidden symbols, Smooth Motion pacing | Faster on the benchmark, and the game plays the same |
| 2. Behavior, not cycles | 0.8 (weeks) | Lighter timing, natives that remove round trips, cached display lists | The benchmark, and plays the same |
| 3. Follow the decompilation | Months | Natives compiled from its source, drawing at the GX/J3D API level, matched scenes ported | Each piece checked against the recompilation |

## Where things stand on October 13

- **HD textures in the app, the same place on every build** (#314): **Mods › Wind Waker HD textures**, first in the
  Mods tab on the Mac, Linux and Windows and in the ⋯ Mods menu on iPhone and iPad: the switch, the texture count and
  adding a pack (the desktop folder picker, Install Texture Pack on iOS). The Mac and Linux menu gained a Mods tab and
  no longer forgets next-launch choices when it is opened again.
- **The importer is built into the app** (#315, draft): a C++ port of `scripts/wwhd` that makes the script's pack
  exactly: from the same discs, the same 3,546 textures with identical pixels (every image's SHA-256), the same
  33,861 files, 124 conflicts and 17 unsupported entries. **Mods › Import from Wind Waker HD…** runs it on the Mac,
  Linux and Windows with progress; on the Mac it made the pack from the menu in about 25 seconds and the next launch
  loaded all 3,546. On iPhone and iPad the same entry is built and installed on Chris's iPad, not yet tried there.
  Still to do: WUX/WUD images, a leaner run for phones (1.07 GB peak on the Mac), Android.
- **iOS menu switches keep the menu open** (#316, #104), installed on the iPad, waiting for a tap test.
- **#313** (Greatfish merchant under his raft): the merchant and his raft load separate archives, and when the
  merchant's finishes first his one ground check misses the raft. Waiting for the reporter's details before a fix.
- Merged: #312 (the release draft), #300 (cforain's build-process docs). Waiting on Chris as on October 12.

## Where things stand on October 12

- **The Wind Waker HD look is in `main`** (#284, #287; RecompCore ee99bbc): distant islands, ambient occlusion, sun and
  cloud shadows, edge light, torch light, HD's far-distance blur and bloom, one switch, off by default, on Mac, Linux,
  Windows and iOS; **F2** switches it in the game on the desktop (#306). HD textures from a real Wii U dump (3,546) load
  with it; the importer installs into the Mac and Linux apps' own folders (#295). Not yet played on Windows, Linux or a
  phone.
- **Fixed in `main`:** the sea chart and the Pictobox (#283: the cache-flush fallback on by default, and a flush keeps
  the GPU's EFB copies, the reporter's red plane). The reporter confirms on the next Windows build.
- **Controls:** the D-pad in Items and Quest Status (#298, #301) and gyro aiming (#302, #304), off by default, on every
  platform but Android; Linux portable mode (pdale-boop, #303); cforain's architecture docs (#282).
- **Speed, measured:** on Chris's iPad Pro the prepared iOS build (combined, lean blocks, memory and deadlines) takes the
  game thread from 81% to 54% at Outset's sea and 53% to 43% in Dragon Roost Cavern, `--behaviour-timing` another 2
  points; on x86 `--behaviour-timing` is 7 to 11% faster (pdale-boop, 27 of 27 pairs); training with the lazy watch on
  (#297) is 4.4 to 5.2% faster on the Mac. The alias resolver's last hit (RecompCore #34) gained nothing and is closed.
- **Flicker (#136):** ComradeClaritux's 120 FPS recording shows Smooth Motion's in-between frames drawing the clouds in the
  wrong place during a fast turn (frames in order). Not reproduced on the Mac; the next step needs a machine that shows it.
- **Waiting on contributors:** #297 to be brought up to date with `main` (pdale-boop); behaviour timing played by a few
  people and on the Deck; the Android port (#93, LiquidAzir) needs `dpad_menus.c` and `hd_look.c` in its build; reporters
  of #263, #245, #188, #108 and #104 to try the next build.
- **Waiting on Chris:** whether the prepared iOS build becomes the iPhone and iPad default (installed on his iPad to
  play); `--behaviour-timing` as a default once people have played it; whether the apps offer HD texture import from a
  menu; when to freeze 0.8.0.

## Where things stand on October 11

- **0.7.0 is out** (October 10, from `66b33be`): Windows zip, the app-only IPA, source and the PadMint recipe. Linux builds
  from source; the AppImage was not in it and still needs a contributor's own-disc build (#56). Each issue in its
  release record was told to try it. The Mayflash adapter (#155) is confirmed fixed and closed.
- **Merged today:** pdale-boop's #242 (the host's per-block checks do less: 7% fewer instructions, 5 to 8% faster on
  slow x86 CPUs, identical game state) and #210 (the developers' stage select and `BLUEWAKE_WARP`, off by default and
  costing nothing when off). Reviewed: every diagnostic switch is set before #242's summary flags, the module already
  runs the interrupt test in `bw_host_quiet`, and the Mac and iPhone builder uses the same `direct_calls.py` watch list.
- **Also merged today:** draw fusion on for Linux (#247, from jkoehler11's Steam Deck run on #215) and Swap L and R (#248, #244).
- **Afternoon:** pdale-boop's #243 (`--host-only` rebuilds in about a minute; I checked its mods step on the Mac with macOS's bash 3.2: byte-identical source, 32 s instead of 42) and #250 (every cutscene in the stage select marked before or after the Helmaroc King fight) merged. #251: the stage select should set the flag a cutscene needs and block saving for the rest of that session. #252 makes Mac, iPhone and iPad builds tiered like the PC builds: 61 minutes of compile instead of 79, and on an iPad Pro the same game-thread load in a trained and an untrained place. Apple builds still compile without the PC builds' preparations (PRIORITIES row 6).
- **Evening:** the docs consolidated: WINDOWS_TASKS.md rewritten around what is in `main`, shipped but unchecked, and checked; the migration log, the Windows checklist, the proposal and the October 7 notes (TECH_DEBT.md) moved to `docs/status/`; MIGRATION_STATUS.md and WINDOWS.md's status made current; README has the Linux guide and an Android answer. #254 (cforain's shared Linux build container) works on a Mac and waits for clang 22 in its image. #255 folded into #59.
- **Android (#93):** every item on its merge checklist is done; speed is the gate (Outset 67 to 80% on the Fold 7 against
  "holds 30"). LiquidAzir has been asked to merge `main` with #242, turn hidden symbols off for Android and measure.
- **New reports:** a 2-core A6 laptop that can't hold 30 at sea, game thread (#246); D-pad in the item screens (#245);
  the screen recorder losing the Vulkan device on Android (#238). The flicker's two earlier switches are ruled out (#136).
- **A new release gate (Chris):** before announcing a release PadMint builds on a Mac, PadMint's
  `scripts/player-check.sh` passes, on the clean checkout before tagging and again after publishing.

## The plan from October 12 (Chris's decisions)

People compare BlueWake with the Wind Waker HD recomp (ZeldaWWHDRecomp), which ships ready-made builds for Windows,
Mac, Linux and Android without game code, builds the game on first start in about two minutes, and reports about
4 ms of CPU a frame. Players want that quality from BlueWake on every platform, Windows, Linux, Android, iPhone and
iPad alike. Chris decided:

- **Speed on by default** (#277): lean memory and lean deadlines in the Windows and Linux builders, the lazy overlap
  watch in the app. 13 to 16.5% less game-thread time on the Mac, the same game state.
- **A "game-behaviour timing" prototype, behind a switch.** The HD recomp is fast because it reproduces the game's
  behaviour, not the console's cycle timing: one copy of each block, no per-instruction bookkeeping. A translation
  mode that does the same here, measured for speed, build time and whether audio and cutscenes hold. If it holds, it
  is also what makes a first-start build in minutes possible ([DIRECTION.md](DIRECTION.md)).
- **The HD look, rebuilt by us**, cheapest first, each an option with the same menu on every platform, off by
  default where the GPU is small. Elliott's October 3 video ("I Backported All The Zelda Wind Waker HD Features to the
  Gamecube Recompile") shows a Wind Waker HD / GameCube switch, sun shadows, cloud shadows on the sea, edge light on
  characters, ambient occlusion, bloom, HD's far-distance blur, light from torches and distant islands drawn 100,000
  units farther, with the HD menus and HUD. None of it is in a public branch; only the texture importer is (and is
  in BlueWake). Chris sends him one last request in parallel.
- **0.8.0 waits for the HD look** (with the Linux AppImage and the speed defaults).

| # | Step | Notes |
| --- | --- | --- |
| H1 | HD textures and HUD from your own Wii U dump, made in the app on every build | **October 13:** the importer is built into the app (#315): it makes the script's pack exactly (3,546 textures, identical pixels) in about 25 s on an M3 Max, from **Mods › Import from Wind Waker HD…** on the Mac, Linux and Windows; the section is the same on every build (#314). Before that, on October 12, the script's pack loaded at Outset with the HD look (frames in #57) and the sea chart with a pack was fixed (#283). Still to do: try it on Windows, Linux and the iPad, WUX/WUD, a leaner run for phones, Android, and the redesigned HD menus. #57 asks for the HD UI |
| H2 | **Distant islands.** The island LOD actor (`d_a_lod_bg.cpp`) shows an island's far model within `scale.x` (its placed scale times 20,000 units), sinks it below the sea past 150,000 and draws with the far plane at 500,000. **In `main` (October 12), off by default:** the host adds 100,000 units to both distances on each island model; [WWHD_LOOK.md](WWHD_LOOK.md) | Game-side, no renderer change; the cheapest |
| H3 | Bloom and the far-distance blur, as passes after the frame in Aurora | **In `main`** (#287, October 12; RecompCore ee99bbc) (RecompCore #26, #27), off by default, on the switch from BlueWake #287 (RecompCore a10b7e4) |
| H4 | Ambient occlusion from the depth buffer | **In `main`** (#287, October 12; RecompCore ee99bbc) (RecompCore #28), on the switch in #287 with H3 |
| H5 | Cloud shadows on the sea | **In `main`** (#287, October 12; RecompCore ee99bbc) (RecompCore #29): the game's camera passed to Aurora, the world rebuilt from depth or the sea plane; on the switch in #287 |
| H6 | Sun shadows over the whole scene, edge light on characters, light from torches and fires | **In `main`** (#287, October 12; RecompCore ee99bbc) (RecompCore #30 to #32, on the switch in #287): sun shadows and edge light from the screen, torch light from the game's point lights. A full shadow map is a later step |
| H7 | The Wind Waker HD / GameCube switch that sets them together | **In with H2:** "Wind Waker HD look" in the Mac, Linux, Windows and iOS menus (`BLUEWAKE_HD_LOOK`); Android when #93 lands |
| T1 | The game-behaviour timing prototype | A switch, measured like every other step. Steps 1 and 2 built October 12, opt-in (`--behaviour-timing`: every block prepaid, the budget checked at loops): 7.0% / 9.1% faster on the Mac, module 8.5% smaller, the same sweep and the same sound ([note](status/BEHAVIOUR_TIMING_2026-10-12.md)). An iPad Pro, October 12, on top of the prepared build: Outset's sea 54% to 52% game-thread busy, the cavern 43% to 41%. x86 (pdale-boop): 7 to 11% faster on an i5-6500 and an i5-12600KF, all 27 pairs. Next: played by a few people (a cutscene with music, the sea) and the Deck; Chris decides any default |

## Now: the speed sprint (from October 11)

**Goal:** BlueWake measurably faster where the CPU is the limit: the Steam Deck, Android phones, two- and four-core
PCs. Every slow report this week says the same thing (`cause=game-thread`, the game thread 97 to 98% busy, the GPU
idle). The translated code does a lot of bookkeeping for each game instruction: it writes the program counter,
charges and checks cycles, records a cycle "observation" around memory instructions, and goes through an inline
check on every memory access; the game's registers live in memory, and every direct call asks the host. The plain
translation that Mac, iPhone and iPad builds compile does more of it than the prepared one the PC builds use.

**The rules for this sprint:** measure before changing anything; one change per pull request, behind a switch,
off by default; a change counts when the guest checkpoints match at the save states and the benchmark is faster.
Chris decides every default change, any merge that changes how the game runs, the release, and the Android bar.

| # | Step | Who | Done when |
| --- | --- | --- | --- |
| S1 | **Done October 11 ([record](status/SPEED_PROFILE_2026-10-11.md)): about a quarter of the CPU time is the game's own work; the rest is the program counter and cycle observation around memory instructions, the dispatcher, exact floating point and the host's per-call checks.** Measure where the game thread's time goes, on this Mac's ARM build with the PC builds' preparations and lean blocks (`--combined-optimizations --lean-blocks`, #257): headless, unpaced, from save states at Outset, the bird scene and Dragon Roost Cavern; a CPU profile split into game work, per-instruction bookkeeping, memory access, the host's per-call checks and the renderer's writes. | Codex | A table in [PERFORMANCE.md](PERFORMANCE.md#results) that ranks the shares |
| S2 | **Fix 1 measured October 12: `--lean-memory` (already in the Windows and Linux builders) is about 5.5 to 6% faster on this Mac and plays the same; contributors asked for x86, Deck and phone numbers; turning it on by default is Chris's decision. On Apple it needs the training link fixed first. Fix 2, October 12: `--lean-deadlines` (new, opt-in in all three builders) is 3.7 to 4.9% faster again, with the same game state; the module is 8.5% larger. The host's per-call checks answered inline in the module: no gain alone. Fix 3: `BLUEWAKE_LAZY_OVERLAP=1` takes the scene overlap watch, which only diagnostics read, out of those checks, and then the inline answer pays: 3.9 to 4.7% faster, same game state ([record](status/SPEED_PROFILE_2026-10-11.md)). All of them together (with the alias resolver's last hit, waiting on Chris) against lean blocks: 13.2% less game-thread time at Outset and 16.5% in Dragon Roost Cavern, untrained, same game state.** Remove the biggest share first, from S1's ranking: (1) the program counter, the cycle observation and the deadline check only on the path that leaves RAM; (2) the host's per-call checks as one inline flag; (3) MSR[FP] checked once per block; (4) fewer dispatches; (5) a last-hit cache for alias lookups. Earlier list: the host's per-call checks become one flag the module reads inline (row 0 below); the memory fast path checks one "slow memory" flag instead of aliases, reservation and write journal separately (RecompCore `cpu.h`); the lean copies write the program counter and the observation value only where an instruction can observe them; the game's registers stay in locals inside a block. | Codex | Each: a switch, matching checkpoints at three states, a "Results" row on the Mac and one from a contributor's PC or phone |
| S3 | **Apple builds with the PC preparations** (PRIORITIES row 6): the same states on a plain Mac build and a prepared one. **October 12: the trained Mac build with lean memory and lean deadlines links now (training at `-O1`) and is 6.3 to 7.4% faster than the trained lean build** ([record](status/SPEED_PROFILE_2026-10-11.md)). Still to do: the plain Mac build in the same table, and an iPad. **October 12, the iPad Pro (M2) baseline** on the installed 0.7.0 tiered build, copied cards, 7,200 retraces: Outset's sea 81% game-thread busy, Dragon Roost Cavern 53%, both at 59.9, matching October 11; the prepared build (combined, lean blocks, memory and deadlines, trained, from `main`'s code): Outset's sea 54% busy and the cavern 43%, both at 59.9, no crash, Chris's save unchanged. A third less game-thread time at Outset; played by hand next. | Codex | Numbers recorded; Chris decides the default |
| S4 | **Prepare the next release in parallel,** without deciding it: what is in `main` since 0.7.0, its checks, and the Linux AppImage as the headline. **Draft: [RELEASE_NEXT_DRAFT_2026-10-11.md](status/RELEASE_NEXT_DRAFT_2026-10-11.md).** | Codex prepares; Chris decides | A release record ready for Chris |

Waiting on Chris: whether Android (#93) merges as experimental below full speed, and when to freeze the release.

## Next: the next release (S4)

**October 12: 0.8.0 waits for the HD look (Chris), with the Linux AppImage and the speed defaults.**

Not dated yet. It carries #242, #210, #243, #247, #248, #250, #252 and #257, plus whatever of row 0 below is ready ([the draft record](status/RELEASE_NEXT_DRAFT_2026-10-11.md)). Before the freeze: the
468-place sweep on the candidate (row 8), Forest Haven played with fusion on Linux, the Linux AppImage from a
contributor's own disc, and `player-check.sh`. Chris decides when to freeze.

## After 0.7.0: catch up with Elliott, then the decompilation

While a release build is running on someone's PC, don't merge code into `main` (docs are fine): the build's check compares `main`'s code with the candidate. One pull request per row, measured on the benchmark (headless, unpaced, from save states) on an x86 PC, each row's
result in PERFORMANCE.md's "Results". Elliott's `windows-release` is cloned at
`~/.codex/work-bluewake-mac-loop/research/Wind-Waker-Recomp`; his reports are in its `docs/status/CURRENT.md`.

| # | Step | Who | Done when |
| --- | --- | --- | --- |
| 0 | **The host's per-call checks** (October 10, the biggest lead). LiquidAzir's profile on the Fold 7 (#93): per retrace, the host and runtime cost 40 M instructions in BlueWake against 7 M in Wind Waker Recomp's build, and `host_direct_can_skip` alone is 23 M. **First part merged October 11: #242** (pdale-boop), 7% fewer instructions on x86, identical game state. Next: the phone and the Deck remeasured on `main`; then the host keeps one "may skip" flag current when its inputs change, which the module reads inline (`cmake/composite/direct_calls.h`), keeping the checks that depend on guest memory. Behind a switch, checked with "plays the same". | Codex; LiquidAzir and jkoehler11 for numbers | #242's share on ARM measured; the inline flag in a pull request with a "Results" row |
| 1 | **Fusion on Mac and Linux.** **Linux merged in #247** (October 11), from jkoehler11's Deck run on #215: a tenth of the draws, nothing missing, the frame rate unchanged because the game thread sets it. Still to do: Forest Haven played on Linux, and on this Mac. | Codex on the Mac; jkoehler11 or cforain on Linux | Linux merged; the Mac played or left off with a reason |
| 2 | **Merged October 10 after the hand-off (#234, #235).** **Early return dispatch and the watch-list fix** (his `return_ranges.py` from `7aca42a`, and `5edeacc`). The `lfs` half of `7aca42a` is #228. **Ported October 10: #235 and #234**, checked on the prepared lean source (811 of 811 dispatches, repeatable, syntax-clean) and by the tests. Held until the Windows 0.7.0 hand-off, so `main`'s code stays the candidate's while it builds. | Codex; a contributor's build | Merged after the hand-off; 2 to 3% on the game thread, plays the same |
| 3 | **Natives round 5** (the GX SDK's FIFO writers, `native_gx_gen.py`) and **round 4** (animation, collision setup, colour), with his comparison tests. First rerun #179's certification on a lean build with jkoehler11's Linux loader (#194): it should certify now. | Codex; jkoehler11 | Certified counts in the build log; 2 to 6% |
| 4 | **Natives round 7** (libm, collision blocks, rotations, geometry, JASystem) and `cache_ops.py`. | Codex | Certified; about 4 points at Forest Haven |
| 5 | **His upload and vertex changes** (RecompCore `6f52a68` to `400728a`), merged by hand with patch 0157. Check dungeon maps (#74), HD packs and lava colours before and after. | Codex | Merged on `bluewake-next`; maps and packs unchanged |
| 6 | **A native from the decompilation's source:** `__ieee754_fmod` from `e_fmod.c` against his replayed one, on the benchmark ([PERFORMANCE.md](PERFORMANCE.md#the-plan)). | Codex | A "Results" row; a yes or no for doing more |
| 7 | **Android:** every merge-checklist item is done (October 10). LiquidAzir merges `main` (#242), turns hidden symbols off for Android (`NOT ANDROID`, 9 M a retrace on ARM64) and measures the pier from a cool start against the 110 M of Wind Waker Recomp's build. | LiquidAzir | Outset holds 30 when cool: merged as "build it yourself, experimental" |
| 8 | **Crash sweep and benchmark tour:** **#210 merged October 11** (no cost when off). Warp to all 468 places on every release candidate; `bench_tour.py` on `BLUEWAKE_WARP`. | pdale-boop, Codex | A sweep in the next release record |
| 9 | **Flicker** (#136): ruled out on 0.7.0 by the reporters: patches 0152 and 0155, the ubershader, the HD texture pack and G-Sync. Next, a look on Chris's PC (AMD) at 120, then the capture in [WINDOWS_TASKS.md](WINDOWS_TASKS.md) section 4 on an NVIDIA PC, against 0.4.0. | A Windows PC, then Codex | Cause named |
| 10 | **Linux video** for social media, shot list on #56, once the 0.7.0 AppImage exists. | cforain or jkoehler11; Chris posts | A clip in hand |

## Late October and November: 0.8

Horizon 2, one pull request and one "Results" row each, off by default until tested on its platform: measure the
bookkeeping left after Elliott's work (a fifth of the module's time lands at block starts, by his profile); try
charging cycles per block with interrupts at block boundaries; natives for the loops that cross chunks most; cache
converted static display lists. Then the graphics thread on four-core CPUs (runbook phase 5).

## On the first of every month

Add a row to PERFORMANCE.md's decompilation table from [decomp.dev](https://decomp.dev/zeldaret/tww). Profile the
benchmark, name the hot functions with the decompilation's `symbols.txt`, list the ones matched since last month,
and pick the next natives from them: replayed from the translation where exactness matters, compiled from source
where step 6 showed it pays. When the actor modules pass about 90% of code matched, revisit porting scenes from
source.
