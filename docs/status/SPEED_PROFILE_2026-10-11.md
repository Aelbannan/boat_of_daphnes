# Where the game thread's time goes (October 11, 2026)

Speed sprint step S1 ([GOAL_LOOP.md](../GOAL_LOOP.md)). Measured on this Mac (M3 Max, ARM64), with the same
preparations the Windows, Linux and Android builds use, to see what the CPU-bound platforms (Steam Deck, phones,
two- and four-core PCs) spend their time on.

## Build and benchmark

- Source: `main` plus #257, built from Chris's own disc with `scripts/builder/build.sh --platform macos
  --combined-optimizations --lean-blocks` (443,155 lean block copies in 813 chunks, trained locally, tiered).
  Training took 35 minutes and the module compiled in 25 (the plain iPad module took 61 the same day).
- Benchmark: headless, unpaced (`BLUEWAKE_RENDERER=headless`, `BLUEWAKE_WALL_PACE=0`, Smooth Motion off), 2,400
  retraces from save states made at retrace 2,390 from copied cards, with `HOME` and every data path pointed at a
  scratch folder.
- Repeatability: three runs at each place gave identical guest checkpoints and block counts, and times within half a
  percent: the sea by Outset 23.4 s (103 retraces a second), Dragon Roost Cavern 15.0 s (160 a second). Later runs
  on a warmer machine took 25.4 to 25.6 s for the same code, so comparisons alternate builds in one session.
- Profile: macOS `sample` at 1 ms for 20 s at Outset. For line attribution the same module was rebuilt with
  `-gline-tables-only` (identical `__text`, 390,188,784 bytes; identical checkpoints and speed), and each sample's
  address was mapped to its source line with `atos -i` and sorted by what the line does.

## Results

Whole game thread, by function:

| Share | What |
| --- | --- |
| 67% | Translated game code (bookkeeping included) |
| 8.6% | The host's per-call checks (`host_direct_can_skip`, `host_chassis_requires_full`, `host_can_skip_observation`), after #242 |
| 6.1% | Memory and register helpers (`ppc_guest_alias_resolve` alone about 2%) |
| 5.1% | Natives |
| 2.9% | Dispatch and cycle charging |

Inside the game module, by source line (sampling can move a few percent between neighbouring lines):

| Share | What |
| --- | --- |
| 22% | The game's own work |
| 13.4% | `ctx->pc = …` before memory instructions |
| 13.2% | The memory fast path (`gather_pipe.h`: a range check, a copy, a byte swap); mostly the game's real loads and stores |
| 9.5% | Inline cycle accounting: `cycle_observation_suffix` and the deadline check after each memory instruction |
| 7.5% | The dispatcher between blocks (`dispatch_loop.h`, after `dispatch()` returns) |
| ~7% | Exact floating point: FPSCR flags and the MSR[FP] check on every float instruction |
| ~5% | Direct-call readiness (`bw_host_quiet`, `bw_direct_call_ready`) |
| ~5% | Cycle helpers and the loop budget |
| ~3% | Natives, alias lookups, the rest |

About a quarter of the CPU time is Wind Waker's own work; the rest keeps the translation cycle-exact and safe.

## What to change, in order

1. **Observation only where it can be observed.** In the lean copies, write the program counter and the cycle
   observation, and check the deadline after a store, only when the access leaves RAM (memory-mapped hardware,
   where the host can see them). Up to about 20% of the module's time. Proof: identical checkpoints.
2. **The host's per-call checks as one flag** the module reads inline, with the full check as the fallback. About
   9 to 14% of the game thread.
3. **Floating point:** check MSR[FP] once per block (safe). Exact FPSCR flags (up to 7%) change behaviour if the
   game reads them, so that one needs Chris's decision.
4. **Fewer dispatches** (7.5%): more direct chaining, longer term.
5. **A last-hit cache for alias lookups** (about 2%): the binary search over about 1,900 ranges runs on every access
   to an actor module's data.
