# PROGRESS — Steam Deck game-thread optimization

Status: M1 and M2 are rejected. M3 candidate investigation is next;
current-source control construction remains blocked by
[#433](https://github.com/chrissotraidis/bluewake/issues/433).

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md) ·
[Game-thread architecture](../../architecture/GameThread.md)

## Completed prerequisite evidence

- [x] Established the Deck environment and verified the profiled AppImage,
  module, state and running Game Mode process.
- [x] Collected repeatable headless, Desktop and Game Mode measurements with
  matching checkpoints and zero-loss CPU profiles.
- [x] Identified the game thread as limiting and mapped its leading translated
  range to `cBgW::GroundCrossGrpRp`.

## Milestones

M0 creates the shared current-source control. Runtime and build-time candidates
then remain separate: compare one change at a time, retain only a passing
candidate, and use that retained result as the next track-specific control.

**Shared control**

### M0: Establish the shared control (depends: candidate ranking, #433)

- [ ] M0-T01: Build the current source with the default Linux recipe. (FR-01)
- [ ] M0-T02: Record inputs, flags, PGO, hashes, size and Deck environment. (FR-01)
- [ ] M0-T03: Repeat the target scene and establish current A/A noise. (FR-02)

**Runtime track**

### M1: Native `GroundCrossGrpRp` (depends: M0 decision) — [rejected and closed in PR #436](https://github.com/chrissotraidis/bluewake/pull/436)

- [x] M1-T01: Map translated memory, calls, cycles and boundaries. (FR-02)
- Not pursued: a full-traversal oracle is unnecessary after the partial hook failed its Deck performance gate.
- [x] M1-T03: Add the opt-in serial entry and activation counters. (FR-03)
- Not pursued: full-path and Windows qualification are unnecessary after the performance no-go.
- [x] M1-T05: Record the A/B decision: reject the partial hook; it is correct for supported paths but not faster than translation on Deck. (FR-04)

### M2: Guest alias resolution (depends: M1 decision) — [rejected in PR #447](https://github.com/chrissotraidis/bluewake/pull/447)

- [x] M2-T01: Capture the fixed-route Deck alias-cost census and CPU sample. (FR-02)
- [x] M2-T02: Compare the observed lookup work with the pinned resolver implementation. (FR-03)
- [x] M2-T03: Reject a new guard: the relevant non-overlapping-alias binary search already exists. (FR-04)

### M3: Native dispatch and numeric paths (depends: M2 decision) — [planned in PR #448](https://github.com/chrissotraidis/bluewake/pull/448)

- [ ] M3-T01: Re-rank dispatch, math and vector self-time after M2. (FR-02)
- [ ] M3-T02: Test serial dispatch reduction before any worker expansion. (FR-03)
- [ ] M3-T03: Require profitable batch sizes before testing parallel work. (FR-05)
- [ ] M3-T04: Run correctness, activation and Deck A/B gates. (FR-04)

### M4: Secondary graphics work (depends: M3 decision)

- [ ] M4-T01: Reconsider GX only if the render thread becomes limiting. (FR-02)
- [ ] M4-T02: Keep render-thread gains separate from game-thread claims. (FR-04)

**Build-time track**

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

**Integration**

### M9: Combine retained candidates (depends: M4, M8)

- [ ] M9-T01: Build only the independently retained runtime/build variants. (FR-04)
- [ ] M9-T02: Run final checkpoints, gameplay, profile and stable-30 gates. (FR-05)

## Evidence boundary

Private AppImages, states, raw logs, and `perf.data` remain on the Deck. The
current evidence uses source-identified control commit
[`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5),
not the blocked current-source control. Desktop runs used isolated default
settings.

## M1 decision record

The detailed implementation and evidence are in [PR #436 (closed)](https://github.com/chrissotraidis/bluewake/pull/436).
The guarded hook accepted 123,609 of 440,495 calls (28.1%); 99.46% of its
declines required unimplemented traversal. Its accepted early return measured
226.1 ns/call native versus 179.3 ns/call translated on Deck. Five interleaved
pairs were within noise, so M1 is rejected.

### M2 decision

The [M2 investigation PR #447](https://github.com/chrissotraidis/bluewake/pull/447)
captured two fixed-route Deck alias-cost reports and a 20-second 99 Hz profile.
The resolver accounted for 1.60% sampled self time, but successful lookups used
about 11 comparisons across roughly 1,900 aliases—the existing sorted-index
binary-search path. No distinct equivalence-safe guard was identified, so M2
is rejected without a RecompCore change. M3 is the next runtime investigation.
