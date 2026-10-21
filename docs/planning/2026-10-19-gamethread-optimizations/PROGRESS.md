# PROGRESS — Steam Deck game-thread optimization

Status: Profiling is in progress. Current-source control construction is
blocked by [#433](https://github.com/chrissotraidis/bluewake/issues/433).

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md) ·
[Game-thread architecture](../../architecture/GameThread.md)

## Milestones

### 1. Setup the Deck profiling environment

- [x] Identify the Deck, OS, power, display, renderer, and profiling tools.
- [x] Record the historical AppImage, module, and state hashes used for the
  current diagnostic runs.
- [ ] Replace the historical artifact with a current-source control after #433
  is resolved. The historical control's Game Mode environment is recorded.

### 2. Run tests and gather metrics

- [x] Run three deterministic five-scene headless sweeps: all 31 checkpoints
  match, with 0.47–1.31% range/mean.
- [x] Run three wall-paced Desktop sweeps over the three slowest scenes: all 19
  checkpoints match, with 29.943–29.979 mean game FPS and 88.55–97.48%
  game-thread busy.
- [x] Capture five symbol-complete, zero-loss, post-settle CPU profiles; more
  than 99.5% of samples in each profile belong to the main thread.
- [x] Capture three matched 30-second Game Mode runs in the Fortress sea scene:
  23.60–23.79 game FPS, 97.39–97.58% busy, 5.48–6.03 ms GX/present waits,
  and 1 ms GPU wait; each profile lost zero samples.

### 3. Rank the best optimization candidates

- [x] Rank the Game Mode investigation surfaces: GX draw-plan construction,
  direct-call skip checks, translated chunks/dispatch, then alias and native
  math/vector paths.
- [ ] Annotate the leading GX and direct-skip call sites and map hot translated
  chunk offsets to guest ranges.
- [ ] Select one bounded candidate after reproducing the ranking with the
  current-source control.

## Evidence boundary

Private AppImages, states, raw logs, and `perf.data` remain on the Deck. The
current evidence uses source-identified historical commit
[`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5),
not the blocked current-source control. Desktop runs used isolated default
settings. The Game Mode result qualifies the historical diagnostic only.
