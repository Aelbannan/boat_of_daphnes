# What we're doing

The dated plan. Ask "what are we doing on October 11?" and the answer is that day's section below. Owner: Chris.
Updated October 11, 2026 (night).

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
| S2 | **Fix 1 measured October 12: `--lean-memory` (already in the Windows and Linux builders) is about 5.5 to 6% faster on this Mac and plays the same; contributors asked for x86, Deck and phone numbers; turning it on by default is Chris's decision. On Apple it needs the training link fixed first. Fix 2, October 12: `--lean-deadlines` (new, opt-in in all three builders) is 3.7 to 4.9% faster again, with the same game state; the module is 8.5% larger. The host's per-call checks answered inline in the module: no gain, so not proposed ([record](status/SPEED_PROFILE_2026-10-11.md)).** Remove the biggest share first, from S1's ranking: (1) the program counter, the cycle observation and the deadline check only on the path that leaves RAM; (2) the host's per-call checks as one inline flag; (3) MSR[FP] checked once per block; (4) fewer dispatches; (5) a last-hit cache for alias lookups. Earlier list: the host's per-call checks become one flag the module reads inline (row 0 below); the memory fast path checks one "slow memory" flag instead of aliases, reservation and write journal separately (RecompCore `cpu.h`); the lean copies write the program counter and the observation value only where an instruction can observe them; the game's registers stay in locals inside a block. | Codex | Each: a switch, matching checkpoints at three states, a "Results" row on the Mac and one from a contributor's PC or phone |
| S3 | **Apple builds with the PC preparations** (PRIORITIES row 6): the same states on a plain Mac build and a prepared one. | Codex | Numbers recorded; Chris decides the default |
| S4 | **Prepare the next release in parallel,** without deciding it: what is in `main` since 0.7.0, its checks, and the Linux AppImage as the headline. **Draft: [RELEASE_NEXT_DRAFT_2026-10-11.md](status/RELEASE_NEXT_DRAFT_2026-10-11.md).** | Codex prepares; Chris decides | A release record ready for Chris |

Waiting on Chris: whether Android (#93) merges as experimental below full speed, and when to freeze the release.

## Next: the next release (S4)

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

