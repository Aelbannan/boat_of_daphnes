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
- [ ] Map `func_802456E0` sampled offsets to guest ranges and annotate the
  leading source-matched call sites.
- [ ] Select one bounded candidate after reproducing the ranking with the
  current-source control.

## Evidence boundary

Private AppImages, states, raw logs, and `perf.data` remain on the Deck. The
current evidence uses source-identified control commit
[`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5),
not the blocked current-source control. Desktop runs used isolated default
settings. The control performs better than the older installed build, whose
detailed measurements are intentionally omitted.
