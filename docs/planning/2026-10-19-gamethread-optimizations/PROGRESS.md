# PROGRESS — Steam Deck game-thread optimization

Status: Profiling is in progress. Current-source control construction is
blocked by [#433](https://github.com/chrissotraidis/bluewake/issues/433).

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md) ·
[Game-thread architecture](../../architecture/GameThread.md)

## Milestones

### 1. Setup the Deck profiling environment

- [x] Identify the Deck, OS, power, display, renderer, and profiling tools.
- [x] Record the control AppImage, module, and state hashes.
- [x] Run the source-identified `3b5c5c4` control in Game Mode and verify its
  AppImage/module hashes before capture.
- [ ] Replace it with a current-source control after #433 is resolved.

### 2. Run tests and gather metrics

- [x] Run three deterministic five-scene headless sweeps: all 31 checkpoints
  match, with 0.47–1.31% range/mean.
- [x] Run three wall-paced Desktop sweeps over the three slowest scenes: all 19
  checkpoints match, with 29.943–29.979 mean game FPS and 88.55–97.48%
  game-thread busy.
- [x] Capture five symbol-complete, zero-loss, post-settle CPU profiles; more
  than 99.5% of samples in each profile belong to the main thread.
- [x] Capture three matched, zero-loss Game Mode profiles of the `3b5c5c4`
  control: 28.25–28.65 estimated game FPS and 97.58–97.72% busy. The three
  windows consistently identify the game thread as the limit.

### 3. Rank the best optimization candidates

- [x] Rank the source-matched Game Mode surfaces: translated chunk
  `func_802456E0`, other translated chunks/dispatch, then native math/vector
  paths. GX draw planning is large but belongs to the secondary render thread.
- [x] Map the leading `func_802456E0` offsets to guest range
  `0x802491F4–0x80249368`, `cBgW::GroundCrossGrpRp`.
- [x] Select a bounded first experiment: a semantics-matched native
  `GroundCrossGrpRp` path with translated fallback and differential tests.
- [ ] Reproduce the ranking with the current-source control after #433 is
  resolved before treating the candidate as qualified for implementation.

## Candidate test order

M0 creates the shared current-source control. Runtime and build-time candidates
then remain separate: compare one change at a time, retain only a passing
candidate, and use that retained result as the next track-specific control.

### M0: Establish the shared control (depends: 3, #433)

- [ ] M0-T01: Build the current source with the default Linux recipe. (FR-01)
- [ ] M0-T02: Record inputs, flags, PGO, hashes, size and Deck environment. (FR-01)
- [ ] M0-T03: Repeat the target scene and establish current A/A noise. (FR-02)

## Runtime candidate sequence

### M1: Native `GroundCrossGrpRp` (depends: M0)

- [ ] M1-T01: Map translated memory, calls, cycles and boundaries. (FR-02)
- [ ] M1-T02: Build an oracle covering trees, callbacks and fallback classes. (FR-05)
- [ ] M1-T03: Add the opt-in serial entry and activation counters. (FR-03)
- [ ] M1-T04: Run differential, native, Linux and Windows checks. (FR-05)
- [ ] M1-T05: Run Deck A/B and retain only if all gates pass. (FR-04)

### M2: Guest alias resolution (depends: M1 decision)

- [ ] M2-T01: Re-profile retained M1 and annotate alias-resolution callers. (FR-02)
- [ ] M2-T02: Test one guarded resolver fast path in RecompCore. (FR-03)
- [ ] M2-T03: Run alias, checkpoint, platform and Deck A/B gates. (FR-04)

### M3: Native dispatch and numeric paths (depends: M2 decision)

- [ ] M3-T01: Re-rank dispatch, math and vector self-time after M2. (FR-02)
- [ ] M3-T02: Test serial dispatch reduction before any worker expansion. (FR-03)
- [ ] M3-T03: Require profitable batch sizes before testing parallel work. (FR-05)
- [ ] M3-T04: Run correctness, activation and Deck A/B gates. (FR-04)

### M4: Secondary graphics work (depends: M3 decision)

- [ ] M4-T01: Reconsider GX only if the render thread becomes limiting. (FR-02)
- [ ] M4-T02: Keep render-thread gains separate from game-thread claims. (FR-04)

## Build-time candidate sequence

This track may run alongside M1–M3 after M0. Each step starts from the last
retained build-time result, without incorporating unqualified runtime changes.

### M5: Compiler version (depends: M0)

- [ ] M5-T01: Compare matched Clang 18 and Clang 22 artifacts. (FR-06)
- [ ] M5-T02: Retain Clang 22 only with repeatable Deck improvement. (FR-06)

### M6: Zen 2 tuning (depends: M5 decision)

- [ ] M6-T01: Add `-mtune=znver2` while retaining `-march=x86-64-v3`. (FR-06)
- [ ] M6-T02: Compare Deck results and preserve the portable default. (FR-06)

### M7: Certified native entries (depends: M6 decision)

- [ ] M7-T01: Build `--native-entries` and verify hook certification. (FR-06)
- [ ] M7-T02: Require activation, differential checks and a Deck gain. (FR-06)

### M8: Deck-scene PGO weighting (depends: M7 decision)

- [ ] M8-T01: Build a separately weighted Fortress/Forest Haven profile. (FR-06)
- [ ] M8-T02: Compare target and out-of-tour scenes for overfitting. (FR-06)

### M9: Combine retained candidates (depends: M4, M8)

- [ ] M9-T01: Build only the independently retained runtime/build variants. (FR-04)
- [ ] M9-T02: Run final checkpoints, gameplay, profile and stable-30 gates. (FR-05)

## Impact evidence

| ID | Actual | Outcome | Evidence |
|---|---|---|---|
| IMP-01 | Not measured | Untested | None; M1 open |
| IMP-02 | Not measured | Untested | None; M0 blocked by #433 |
| IMP-03 | Not measured | Untested | None; M5–M8 wait for M0 |

## Evidence boundary

Private AppImages, states, raw logs, and `perf.data` remain on the Deck. The
current evidence uses source-identified control commit
[`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5),
not the blocked current-source control. Desktop runs used isolated default
settings. The control performs better than the older installed build, whose
detailed measurements are intentionally omitted.
