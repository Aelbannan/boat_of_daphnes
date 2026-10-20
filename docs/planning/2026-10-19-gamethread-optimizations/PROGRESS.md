# PROGRESS — Steam Deck game-thread optimization

Status: Planned; previous #421 measurements and conclusions discarded

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md)

## Milestones

### M0: Fresh Steam Deck baseline

Architecture: [performance workflow](../../performance/Overview.md#performance-workflow).

- [ ] M0-T01: Freeze source/build identity, Deck environment, private scene inputs, settings, metrics, and retention policy. (FR-01, FR-04)
- [ ] M0-T02: Run same-build A/A headless, rendered, and Game Mode measurements; record noise and reject incomparable runs. (FR-01)
- [ ] M0-T03: Capture flat/per-thread profiles and annotations; rank code candidates with estimated upper bounds and activation evidence. (FR-02)

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
| IMP-01 | Not measured | Planned | None |
| IMP-02 | Not selected | Planned | None |
| IMP-03 | Not measured | Planned | None |
| IMP-04 | Not measured | Planned | None |
| IMP-05 | Not tested | Planned | None |

## Evidence ledger

No tasks are complete. Planning/source inspection is not performance,
correctness, or hardware evidence.
