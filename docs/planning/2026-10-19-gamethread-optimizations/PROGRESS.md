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

### M0: Freeze the native contract (depends: 3)

- [ ] M0-T01: Map the translated blocks, memory, calls, cycles and boundaries. (FR-02)
- [ ] M0-T02: Record the unchanged-state fallback and serial traversal contract. (FR-03)

### M1: Build the differential oracle (depends: M0)

- [ ] M1-T01: Cover supported trees, callbacks and every fallback class. (FR-05)
- [ ] M1-T02: Prove native activation; serial fallback cannot satisfy the test. (FR-05)

### M2: Implement the opt-in native entry (depends: M1)

- [ ] M2-T01: Add the serial entry, certified hook and activation counters. (FR-03)
- [ ] M2-T02: Run differential, native, Linux and Windows checks. (FR-05)

### M3: Qualify on Steam Deck (depends: M2, #433)

- [ ] M3-T01: Build matched artifacts and record inputs, hashes and environment. (FR-01)
- [ ] M3-T02: Run interleaved Deck comparisons and re-profile activation. (FR-02)
- [ ] M3-T03: Retain only if correctness, cost and stable-30 gates pass. (FR-04)

### M4: Evaluate Deck build-time variants (depends: #433)

Parallel with M0–M2; it changes builder/compiler inputs, not collision code.

- [ ] M4-T01: Compare matched Clang 18 and Clang 22 Deck artifacts. (FR-06)
- [ ] M4-T02: Add and compare an `x86-64-v3` plus `znver2` tuning candidate. (FR-06)
- [ ] M4-T03: Measure certified native entries with hook and activation evidence. (FR-06)
- [ ] M4-T04: Compare a separately weighted Deck-scene PGO recipe. (FR-06)
- [ ] M4-T05: Measure only independently successful variants together. (FR-06)

## Impact evidence

| ID | Actual | Outcome | Evidence |
|---|---|---|---|
| IMP-01 | Not measured | Untested | None; M1–M2 open |
| IMP-02 | Not measured | Untested | None; M3 blocked by #433 |
| IMP-03 | Not measured | Untested | None; M4 blocked by #433 |

## Evidence boundary

Private AppImages, states, raw logs, and `perf.data` remain on the Deck. The
current evidence uses source-identified control commit
[`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5),
not the blocked current-source control. Desktop runs used isolated default
settings. The control performs better than the older installed build, whose
detailed measurements are intentionally omitted.
