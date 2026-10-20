# PROGRESS — Steam Deck game-thread optimization

Status: M0 in progress; the reproducible control is blocked by #433. Preliminary
Deck evidence uses an existing, source-unverified private AppImage.

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md)

Architecture: [game thread](../../architecture/GameThread.md) ·
[test evidence](../../architecture/Testing.md)

## Milestones

### M0: Fresh Steam Deck baseline

Architecture: [performance workflow](../../performance/Overview.md#performance-workflow).

- [ ] M0-T01: Freeze source/build identity, Deck environment, private scene inputs, settings, metrics, and retention policy. (FR-01, FR-04)
- [ ] M0-T02: Run same-build A/A headless, rendered, and Game Mode measurements; record noise and reject incomparable runs. (FR-01)
- [ ] M0-T03: Capture flat/per-thread profiles and annotations; rank code candidates with estimated upper bounds and activation evidence. (FR-02)

### M0 execution record

This record retains only hashes and redacted measurements. Private AppImages,
states, raw logs, and `perf.data` remain on the Deck.

| Task | Current evidence | Limitation |
|---|---|---|
| M0-T01 | Steam Deck Jupiter; SteamOS 3.8.28; kernel 6.18.50; plugged in; `powersave`; `perf` 6.15. Existing microSD AppImage SHA-256: `abe940206989975ea91a412b6c9780953dfb52d099d4e4f3b58b961cd7aeac07`. | The AppImage source/build receipt is unknown; it is not the pinned `2ab8118` control. |
| M0-T02 | Fixed-state headless A/A runs: 34.281, 34.407, and 34.381 game FPS; 99.0% busy each; comparisons differ by +0.4% and +0.3%; all six checkpoints match. | Rendered and Game Mode measurements remain pending. |
| M0-T03 | 30-second headless `perf` capture: 2K samples, zero lost. Leading flat samples include translated chunks, `bluewake_native_game_math_try`, `chassis_dispatch`, `bluewake_native_vec_try`, and `ppc_guest_alias_resolve`. | No source receipt, per-thread report, or symbol annotation yet; this cannot select an implementation candidate. |

### M1: One bounded candidate (depends: M0)

Owns only the code surface selected by M0; candidate-specific test preparation
may proceed separately after its semantic interface is frozen.

- [ ] M1-T01: Define preserved state, arithmetic, deadlines, ordering, fallback, and lifecycle behavior. (FR-03, FR-05)
- [ ] M1-T02: Implement one opt-in candidate and positive activation reporting; keep timing instrumentation out of measured binaries. (FR-03)
- [ ] M1-T03: Add focused source/guard and differential-oracle coverage, including decline/fallback cases. (FR-03, FR-05)

### M2: Matched performance comparison (depends: M1)

Parallel with M3's final test execution only where the Deck and test resources do
not overlap.

- [ ] M2-T01: Produce matched A/B builds and receipts; verify hashes and candidate activation. (FR-01, FR-03)
- [ ] M2-T02: Run five interleaved headless/rendered pairs plus the Game Mode scene; compare cost, throughput, tails, waits, clocks, and noise. (FR-04)
- [ ] M2-T03: Re-profile the candidate and explain where saved time went; record stable-30 result or remaining gap. (FR-02, FR-04)

### M3: Correctness and platform qualification (depends: M1)

- [ ] M3-T01: Run applicable #424 pool/lifecycle coverage, candidate tests, private oracle, and Linux/Windows CI. (FR-05)
- [ ] M3-T02: Play the rendered Deck route with cutscene/audio, sailing/input, save/reload, and clean shutdown. (FR-05)
- [ ] M3-T03: Check the low-load control and record regressions, skipped lanes, and incomplete evidence explicitly. (FR-04, FR-05)

### M4: Decision and submission (depends: M2, M3)

- [ ] M4-T01: Reconcile performance and correctness evidence against both success gates. (FR-04, FR-05)
- [ ] M4-T02: Retain, revise, or reject the candidate; document any remaining bottleneck without carrying unsupported conclusions forward. (FR-02, FR-04)
- [ ] M4-T03: Submit one scoped upstream PR with commands, redacted evidence, limitations, and rollback/default policy. (FR-03, FR-05)

## Impact evidence

| ID | Actual | Outcome | Evidence |
|---|---|---|---|
| IMP-01 | Headless A/A is within 0.4%; checkpoints match | Partial | M0 execution record; rendered/Game Mode and reproducible-control evidence pending |
| IMP-02 | Not selected | Planned | None |
| IMP-03 | Not measured | Planned | None |
| IMP-04 | Not measured | Planned | None |
| IMP-05 | Guest checkpoints match across the fixed-state headless A/A runs | Partial | M0 execution record; candidate and platform qualification pending |

## Evidence ledger

No task is complete. Planning/source inspection is not performance,
correctness, or hardware evidence; preliminary hardware evidence does not
replace the pinned control or the remaining rendered/Game Mode work.
