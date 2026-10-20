# PROGRESS — Steam Deck game-thread optimization

Status: M0 in progress; the reproducible control is blocked by #433. Preliminary
Deck evidence uses an existing, source-unverified private AppImage.

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md)

Architecture: [game thread](../../architecture/GameThread.md) ·
[test evidence](../../architecture/Testing.md)

## Milestones

### M0: Fresh Steam Deck baseline

Architecture: [performance workflow](../../performance/Overview.md#performance-workflow).

- [ ] M0-T01: Freeze source/dependency/build receipt; AppImage/module/state hashes; Deck, power, display, renderer, and settings metadata. (FR-01, FR-04)
- [ ] M0-T02: Run at least three same-build A/A headless, rendered Desktop Mode, and Game Mode measurements; record checkpoints, FPS, busy/dips, and environmental noise. (FR-01)
- [ ] M0-T03: Capture flat/per-thread `perf`, call graphs/annotations, cycles/instructions per retrace, and renderer/GPU/presentation waits where available; rank one candidate only after the M0 gate. (FR-02)

### M0 execution record

This record retains only hashes and redacted measurements. Private AppImages,
states, raw logs, and `perf.data` remain on the Deck.

| Task | Current evidence | Limitation |
|---|---|---|
| M0-T01 | Steam Deck Jupiter; SteamOS 3.8.28; kernel 6.18.50; AC power; `powersave`; `perf` 6.15; active Plasma Wayland Desktop Mode; collection temperatures 29°C Deck sensor / 48°C APU. Historical AppImage source `3b5c5c4826ef62bd1f8ff387734ff2ef295898e5`, SHA-256 `2fdd36b90d33f2d0068a03a46ff07629b2f5738929aefa7b96a89a68d2dd2b6b`; module SHA-256 `0775a84cc1e1042fff25df8fc05d506ce125d5a4da1c7d56065344e28e28c552`. | This is a source-identified historical diagnostic, not the pinned `2ab8118` control. Frame cap, refresh, TDP, texture/options receipt, and Game Mode receipt remain unrecorded. |
| M0-T02 | Historical fixed-state A/A, six matching checkpoints each. Headless: 34.0477, 34.1136, 34.4452 FPS; 98%, 98%, 99% busy; maximum spread +1.2%. Rendered Desktop Mode: 31.1062, 30.7188, 30.6021 FPS; 97.875%, 98.208%, 98.125% busy; maximum spread -1.6%. | Game Mode and a second scene/low-load control remain pending. Desktop session logs report only startup game-thread dips; this is not a Game Mode qualification. |
| M0-T03 | 30-second headless process-attached cycle sample: 2K samples, zero lost, ~96.0B cycles. Leading self time: `func_802456E0` 7.80%, `func_803256E0` 3.90%, `bluewake_native_game_math_try` 3.29%, `func_802ED6E0` 3.15%, `chassis_dispatch` 2.70%, `ppc_guest_alias_resolve` 2.13%. Annotations confirm translated dispatch/guest-state checks in the leading chunk and native-path guards in the math path. | `perf stat -a` is denied by `perf_event_paranoid=2`; no instructions-per-retrace, reliable thread split, or Game Mode profile yet. The resulting ranking is investigation order only. |
| Historical diagnostic | The AppImage launches directly on Deck, loads `quick-23874.bwstate` with `missing=0` and `mismatched=0`, and executed beyond retrace 31,000. Raw JSON, logs, and `perf.data` stay private under `~/bluewake-profile/`. | An extract-and-run wrapper previously misidentified the child PID; direct AppImage profiling works. This does not make the artifact a release candidate. |

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
| IMP-01 | Historical A/A: headless maximum 1.2%; rendered Desktop maximum 1.6%; checkpoints match | Partial | M0 execution record; Game Mode and reproducible-control evidence pending |
| IMP-02 | Not selected | Planned | None |
| IMP-03 | Not measured | Planned | None |
| IMP-04 | Not measured | Planned | None |
| IMP-05 | Guest checkpoints match across the fixed-state headless A/A runs | Partial | M0 execution record; candidate and platform qualification pending |

## Evidence ledger

No task is complete. Planning/source inspection is not performance,
correctness, or hardware evidence; preliminary hardware evidence does not
replace the pinned control or the remaining rendered/Game Mode work.
