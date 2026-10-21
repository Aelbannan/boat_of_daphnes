# TECH_PLAN — Steam Deck game-thread optimization

Status: Planning only

Source: upstream `main` at `2ab8118fc09fa08569100201a871616d18a1b771`

[PRD](PRD.md) · [Tracker](PROGRESS.md)

Architecture: [game thread](../../architecture/GameThread.md) ·
[test evidence](../../architecture/Testing.md) ·
[performance workflow](../../performance/Overview.md)

## Inspected facts

- [`bench_tour.py`](../../../scripts/bench_tour.py) controls fixed scenes and
  emits speed, busy time, dip causes, and guest checkpoints in `summary.json`.
- [`profile_deck.sh`](../../../scripts/linux/profile_deck.sh) launches one state,
  samples the Linux process with `perf`, and emits `perf.data`, `report.txt`,
  `run.log`, and a session-log copy. Headless and rendered runs answer different
  questions; neither invokes the benchmark tour.
- [`native_work_pool.c`](../../../cmake/composite/native_work_pool.c) provides
  synchronous range work on POSIX; Windows remains serial. #424 adds broader
  pool/lifecycle coverage but is not evidence that any game candidate is faster.
- No old #421 result is retained. Baseline, noise, hotspots, and activation are
  **unknown/not measured** for this plan.

## Planned changes

| Track / milestone | Code or command | Produced artifact | Test and decision |
|---|---|---|---|
| Shared control — M0 | [`build.py --ci-container --appimage`](../../../scripts/linux/build.py), [`bench_tour.py`](../../../scripts/bench_tour.py), [`profile_deck.sh`](../../../scripts/linux/profile_deck.sh) | Hashed AppImage/module, build provenance, A/A summaries and `perf.data` | Repeat one unchanged build until noise is known; all later candidates use the same source, state and Deck settings |
| Runtime 1 — M1 collision | [`native_bg.c`](../../../cmake/composite/native_bg.c), [`native_entries.py`](../../../scripts/windows/native_entries.py), [`native_bg_test.c`](../../../tests/native_bg_test.c) | Opt-in `GroundCrossGrpRp` native entry, certified hook, activation count and differential report | CPU state/RAM must match the translation; retain when the Deck result beats M0 beyond noise |
| Runtime 2 — M2 alias resolver | RecompCore [`cpu.c`](../../../ref/recompcore/GXRuntime/src/core/cpu.c) through its required upstream PR/pin/patch flow | One guarded resolver fast path plus alias/call-site evidence | Run resolver tests, checkpoints and Deck A/B against the retained runtime control |
| Runtime 3 — M3/M4 dispatch and GX | [`dispatch_loop.h`](../../../cmake/composite/dispatch_loop.h), [`native_math.c`](../../../cmake/composite/native_math.c), [`native_work_pool.c`](../../../cmake/composite/native_work_pool.c); GX only if thread ownership changes | One measured serial fast path, or a positively activated batch large enough to amortize workers | Reject serial fallback as parallel evidence; keep secondary-render-thread gains separate from game-thread gains |
| Build 1 — M5/M6 compiler | Existing `--clang22`; proposed recorded `-mtune=znver2` with `-march=x86-64-v3` | Clang 18, Clang 22 and Zen 2-tuned AppImages with matching profile/input hashes | Compare one compiler variable at a time; Deck tuning does not change the portable default |
| Build 2 — M7/M8 preparation | Existing `--native-entries`; proposed separately fingerprinted Fortress/Forest Haven PGO recipe | Hook/activation report, profile hash, module size and target/out-of-tour summaries | Require certified active hooks; reject PGO overfitting or regressions outside the emphasized scenes |
| Combined — M9 | Build only independently retained runtime and build variants | Final private AppImage and qualification record | Re-run checkpoints, gameplay, profiling and A/B; record any remaining gap to 30 FPS |

## Steam Deck procedure

Keep the control immutable. Build candidates from the same source revision,
compiler, PGO inputs, options, and AppImage environment; change only the
candidate. Never publish the personal module, state, card, or raw private log.

### Optional: SSH setup and limits

SSH can drive the headless benchmarks, profiles, comparisons, and evidence
collection. On the Deck, enter Desktop Mode, open Konsole, set a password if
needed, start the server, and obtain its LAN address:

```bash
passwd
sudo systemctl enable --now sshd
hostname -I
```

From the development machine, connect as `deck` and copy the private test bundle
only between machines you control:

```bash
ssh deck@DECK_IP
rsync -av ./deck-test/ deck@DECK_IP:~/bluewake-test/
```

Prefer SSH keys over leaving password authentication exposed. To stop and
disable the service after testing, run `sudo systemctl disable --now sshd` on
the Deck.

| Task | From SSH? | Boundary |
|---|---|---|
| Copy AppImages/scripts and collect redacted evidence | Yes | Keep the personal AppImage, state, card, module, and raw logs private |
| Headless `bench_tour.py`, `profile_deck.sh`, `perf report`, hashes, and comparisons | Yes | No display session is required |
| Rendered Desktop Mode benchmark/profile | Partly | A local graphical session must already be active; SSH needs its valid `DISPLAY` and `XAUTHORITY` |
| Game Mode launch and gameplay checks | No | Start and play the Steam entry on the Deck |
| Attach `perf` to an already-running Game Mode process and retrieve logs | Yes | Launch locally first, then attach by the verified process ID over SSH |

Commands in the following sections run in an SSH shell unless they explicitly
require Desktop Mode or Game Mode. Do not use X11 forwarding as a substitute
for measuring the Deck's own rendered session.

### 1. Build and identify the control

On the development machine:

```bash
git switch --detach 2ab8118fc09fa08569100201a871616d18a1b771
python3 scripts/linux/build.py /path/to/GZLE01.iso \
  --out build/deck-control --ci-container --appimage
sha256sum build/deck-control/BlueWake-x86_64.AppImage \
  build/deck-control/composite/gGZLE01_recomp.so
```

Record the commit, dependency-lock hash, compiler, builder command, AppImage and
module hashes, PGO/training status, and build summary. Copy the AppImage plus
`scripts/bench_tour.py` and `scripts/linux/profile_deck.sh` to the Deck. Both
the default Clang 18 and optional Clang 22 `x86-64-v3` container paths are
currently blocked by [#433](https://github.com/chrissotraidis/bluewake/issues/433);
neither is a valid control until that issue is resolved.

### Verified profiling control

[`3b5c5c4826ef62bd1f8ff387734ff2ef295898e5`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5)
is the source revision used for the recorded measurements. It compiles the
Clang 22 application
through the earlier packaged-Dawn route (RecompCore
`77d4717d868cec8b4014932bb22978c284a535a7`). Its AppImage ABI audit fails on
`GLIBCXX_3.4.32`, but that warning is not a Deck compatibility verdict: the
resulting AppImage launched on the Deck, loaded the fixed state, and executed
gameplay. It is the profiling control, not a release candidate.

### Deck test bundle

Copy only the private AppImage, the two measurement scripts, and selected save
states. Do not copy the build tree, ISO, generated sources, standalone module,
or local logs.

```text
~/bluewake-test/
├── BlueWake-x86_64.AppImage
├── bench_tour.py
├── profile_deck.sh
└── states/
    └── quick-N.bwstate
```

With `bench_tour.py --state states/quick-N.bwstate`, its positional card
argument is unused; pass `unused.card`. The AppImage already contains the
private translated module and disc copy, so retain it only on machines you
control.

### 2. Freeze the Deck environment and private workload

Record Steam Deck revision, SteamOS/kernel, Game Mode or Desktop Mode, refresh
rate, frame cap, TDP/clock limits, power source, renderer, resolution, textures,
Smooth Motion, and state hash. Use one heavy state (initially Outset), another
measured slow scene, and a low-load control. Fix cooling/power conditions and
discard warmup before comparing runs.

### 3. Establish same-build noise

From the Deck, run the control at least three times per mode:

```bash
python3 scripts/bench_tour.py unused.card ./BlueWake-x86_64.AppImage \
  --state /home/deck/.local/share/BlueWake/quick-N.bwstate --mode headless
mv summary.json evidence/control-headless-1.json

python3 scripts/bench_tour.py unused.card ./BlueWake-x86_64.AppImage \
  --state /home/deck/.local/share/BlueWake/quick-N.bwstate --mode rendered
mv summary.json evidence/control-rendered-1.json

python3 scripts/bench_tour.py compare \
  evidence/control-headless-1.json evidence/control-headless-2.json
```

Repeat with identical settings. If same-build speed varies by more than 3%, or
checkpoints differ, fix the workload before profiling candidates.

### 4. Capture CPU profiles

Headless/unpaced isolates execution throughput; rendered/Desktop Mode exposes
GX, GPU, and presentation waits:

```bash
scripts/linux/profile_deck.sh --seconds 30 \
  --appimage ./BlueWake-x86_64.AppImage \
  --state /home/deck/.local/share/BlueWake/quick-N.bwstate \
  --out evidence/profile-headless

scripts/linux/profile_deck.sh --rendered --seconds 30 \
  --appimage ./BlueWake-x86_64.AppImage \
  --state /home/deck/.local/share/BlueWake/quick-N.bwstate \
  --out evidence/profile-rendered
```

For the primary Game Mode capture, launch the same AppImage/state through Steam,
then attach over SSH rather than launching a second process:

```bash
PID=$(pgrep -n -f 'mount_.*bluewake')
perf record -g -F 99 -p "$PID" -o evidence/game-mode.data -- sleep 30
perf report -i evidence/game-mode.data --stdio --no-children \
  > evidence/game-mode-flat.txt
perf report -i evidence/game-mode.data --stdio --sort=tid,symbol \
  > evidence/game-mode-threads.txt
```

Reject a capture with the wrong PID/scene, lost samples, thermal/power changes,
missing symbols, or insufficient post-warmup duration.

### 5. Rank and investigate candidates

Correlate flat self-time, per-thread ownership, session `busy`/wait fields, and
cycles/instructions per retrace. Use `perf annotate` on each leading symbol.
Generated `func_<address>` symbols are chunks, not original game functions;
map ranges before naming game logic.

### 6. Compare a candidate

Build A and B from the same revision and inputs. Run five interleaved pairs
(`A/B/B/A/...`) in headless and rendered modes, then repeat the Game Mode scene.
Use `bench_tour.py compare`, verify candidate activation, compare medians and
noise, re-profile B, and complete the gameplay route. A candidate qualifies
when it activates, preserves correctness, and improves on the matched control
beyond noise. Reaching 30 FPS completes the broader performance goal; it is not
required to retain an independently useful improvement.

```mermaid
flowchart TD
    Control["Immutable<br/>control"] --> Noise["A/A<br/>noise"]
    Noise --> Profile["Headless, rendered,<br/>and Game Mode<br/>profiles"]
    Profile --> Rank["Rank exclusive<br/>cost and waits"]
    Rank --> Candidate["One bounded<br/>candidate"]
    Candidate --> Compare["Interleaved<br/>A/B runs"]
    Compare --> Gates{"Activated, correct,<br/>and faster than<br/>control beyond noise?"}
    Gates -->|"yes"| Retain["Retain<br/>candidate"]
    Gates -->|"no"| Record["Reject or<br/>iterate"]
    Retain --> Goal{"Stable<br/>30 FPS?"}
    Goal -->|"yes"| Complete["Performance<br/>goal met"]
    Goal -->|"no"| Continue["Record remaining gap<br/>and test next candidate"]
```

## Build-time optimization experiments

The [build pipeline](../../architecture/Build.md) already prepares the module,
trains an LLVM profile, compiles hot and cold chunks separately, and packages
the resulting host and module. Linux defaults already include `x86-64-v3`, PGO,
lean blocks/memory/deadlines, behaviour timing, direct calls, fixed guest state,
the gather pipe and the established native groups.

| Priority | Experiment | Required change | Decision boundary |
|---:|---|---|---|
| 1 | Clang 18 versus optional Clang 22 | Use the existing [`--clang22`](../../../scripts/linux/build.py) container path | Same source, profile and flags; retain only a repeatable Deck gain |
| 2 | Zen 2 scheduling | Add a recorded `-mtune=znver2` option while retaining `-march=x86-64-v3` | Deck-specific candidate; do not change the portable default from Deck evidence alone |
| 3 | Certified native entries | Build with [`--native-entries`](../../../scripts/linux/build.py) and retain its hook/activation reports | Require certified hooks, positive activation, differential tests and a gain beyond noise |
| 4 | Deck-scene PGO weighting | Add a separate training recipe emphasizing the measured Fortress and Forest Haven routes | Compare outside the emphasized scenes; reject overfitting or code-size regressions |

Run these as isolated A/B experiments rather than one cumulative build. Only
after each contribution is known should a final combined candidate be measured.
Record compiler version, effective flags, profile hash, module hash and module
size for every artifact.

Do not pursue `--no-cold`/`--no-tiered` first: existing project measurements
found the larger all-`-O2` module slower. `-O3` has no current performance case.
`--native-gx` and `--native-set4` have no active hooks, and full-module LTO has
no measured benefit proportional to its compile/link risk.

## Findings

These are findings from the source-identified profiling control. The raw
AppImage, state, logs, and `perf.data` remain private.

### Environment

| Item | Measured value |
|---|---|
| Hardware / OS | Steam Deck Jupiter; SteamOS 3.8.28; kernel 6.18.50 |
| Power / CPU policy | AC power; `powersave`; 2.42 GHz observed during collection; reported power cap `15000000` |
| Temperature | 29°C Deck sensor; 48°C APU |
| Sessions | Plasma Wayland Desktop Mode on an external 1920×1080/120 Hz display; Game Mode at 1280×800 |
| Controlled rendered configuration | Vulkan/Mailbox; 960×720 host window and framebuffer; scale 1; default GXCore renderer; blank isolated settings, so no HD texture pack or personal options |
| Artifact | Source [`3b5c5c4`](https://github.com/chrissotraidis/bluewake/commit/3b5c5c4826ef62bd1f8ff387734ff2ef295898e5); AppImage `2fdd36…2dd2b6b`; module `0775a8…e28c552` |
| Instrumentation | `perf` 6.15; process-attached cycle sampling works; system-wide `perf stat` is denied by `perf_event_paranoid=2` |
| Worker pool | The benchmark runs were serial because `bench_tour.py` clears inherited `BLUEWAKE_*` settings. A separate two-worker probe also reported `parallel=0`: its largest vector array was 8 items, below the 768-item split threshold (`256 × 3` participants). |

### Measurement methodology

- Loaded the same private `quick-23874.bwstate`. The original single-scene A/A
  runs produced matching checkpoints.
- Ran three five-scene headless sweeps with the benchmark's explicit virtual-pad
  pulse sequence. Every requested scene loaded, all 31 checkpoints matched
  across all runs, and every scene stayed below the 3% same-build noise guide.
- Ran three wall-paced rendered sweeps over the three slowest headless scenes in
  the active Desktop session. Every requested scene loaded and all 19
  checkpoints matched. These controlled runs used isolated default settings,
  not the personal HD-texture/options configuration.
- Attached `perf record -g -F 99` to one 30-second headless run. It captured
  2K user-space cycle samples with zero loss. `perf annotate` was used to
  inspect the leading symbols.
- Ran the guide's wall-paced rendered command,
  [`profile_deck.sh --rendered`](../../../scripts/linux/profile_deck.sh), in
  the Deck's local Wayland session. Its post-warmup samples were 27–29 game
  FPS, `busy=97%`, `cause=game-thread`, `workers: gx=32–38%`, and `gpu=1ms`.
  This is a single Desktop Mode capture, not a Game Mode qualification.
- Ran one non-comparative, headless probe with `BLUEWAKE_NATIVE_WORKERS=2` and
  allowed normal shutdown so the native-math counters were printed. It is
  activation evidence only, not an A/B result.
- Captured one post-settle 30-second `perf` profile per scene. Starting `perf`
  after the eight-second settle retained the module mapping events, so symbols
  resolved in every report. More than 99.5% of samples in every capture
  belonged to the main thread; all captures lost zero samples.

### Five-scene workload survey

These are unpaced computational-throughput results, not player-visible FPS.
All scene windows stayed below the 3% same-build noise guide and checkpoints
were identical. Earlier divergence came from omitting the explicit virtual-pad
sequence, so those runs are excluded.

| Scene | Mean game frames/s | Range / mean | Relative cost |
|---|---:|---:|---|
| Sea by Forsaken Fortress | 33.63 | 0.52% | Slowest |
| Forest Haven | 36.35 | 0.47% | Second slowest |
| Outset | 40.76 | 0.73% | Third slowest |
| Windfall | 43.14 | 0.95% | Fourth |
| Dragon Roost Cavern | 58.57 | 1.31% | Fastest / low-load control |

### Rendered Desktop slow-scene survey

These are wall-paced observations in the controlled Desktop configuration
listed above. They establish repeatability and expose the active wait domain;
they are not Game Mode or representative-HD-texture qualification.

| Scene | Mean game FPS | Range / mean | Mean game-thread busy |
|---|---:|---:|---:|
| Sea by Forsaken Fortress | 29.979 | 0.55% | 97.48% |
| Forest Haven | 29.943 | 0.25% | 96.39% |
| Outset | 29.955 | 0.03% | 88.55% |

Only transient startup/load samples fell below target after warmup. Their
reported cause was `game-thread`, with zero recorded GPU-wait milliseconds.

### Source-matched Game Mode capture

The Steam shortcut was retargeted to the verified `3b5c5c4` control. The
running AppImage hash was `2fdd36…2dd2b6b`; its loaded module hash was
`0775a8…e28c552`. Three matched 30-second Fortress sea captures lost zero
samples.

| Run | Estimated game FPS | Range | Busy | Leading main-thread symbol |
|---|---:|---:|---:|---|
| 1 | 28.65 | 26.15–29.65 | 97.72% | `func_802456E0` 4.55% |
| 2 | 28.46 | 26.60–28.95 | 97.63% | `func_802456E0` 4.28% |
| 3 | 28.25 | 25.30–28.70 | 97.58% | `func_802456E0` 4.04% |

Game FPS is derived from every per-second retrace-rate sample, not only
`fps-dip` lines. `build_draw_plan_into` is 4.10–4.49% of process samples but
runs on the secondary render thread. The module reports
`direct-calls=on inline-answer=on`; the old host callback hotspot is absent.
This control also improves over the older installed build used before the
profiling artifact was pinned.

### Mapped leading candidate

The leading annotated offsets in `func_802456E0` map to guest range
`0x802491F4–0x80249368`: Wind Waker's
[`cBgW::GroundCrossGrpRp`](https://github.com/zeldaret/tww/blob/main/src/SSystem/SComponent/c_bg_w.cpp#L577-L602).
It filters a collision group, checks its bounds and ground tree, then recursively
visits child groups. Nearby samples map to other `cBgS`/`cBgW` collision paths,
including registration and line crossing.

BlueWake does not currently replace `GroundCrossGrpRp` natively. A first
candidate should preserve its guest-visible registers, cycle/deadline behavior,
group-pass callbacks, traversal order, and updates to the shared ground-check
result. That state dependency makes parallel child traversal unsafe without a
separate equivalence proof; a semantics-matched native path with translated
fallback is the bounded first experiment.

### Native reimplementation workstream

This is a serial replacement of one measured guest routine. It follows the
existing [`native_bg.c`](../../../cmake/composite/native_bg.c) contract: inspect
all prerequisites before changing state, execute the native path only when its
equivalence is known, and otherwise leave the CPU and RAM untouched so normal
translation can run.

```mermaid
flowchart TD
    Entry["GroundCrossGrpRp entry"] --> Guard{"RAM, timing, callbacks<br/>and traversal supported?"}
    Guard -->|"No; no mutation"| Guest["Run translated routine"]
    Guard -->|"Yes"| Native["Run serial native traversal<br/>in original child order"]
    Native --> Commit["Commit registers, memory,<br/>PC and cycle state"]
    Guest --> Return["Return to caller"]
    Commit --> Return
```

| Status | File / symbol | Practical edit | Contract |
|---|---|---|---|
| Modify | [`native_bg.c`](../../../cmake/composite/native_bg.c) / `bluewake_native_bg` | Add the `0x802491F4` entry and a guarded serial implementation | Decline without mutation unless the complete path is supported |
| Modify | [`native_bg.h`](../../../cmake/composite/native_bg.h) | Name the entry address and document the expanded collision scope | Keep one address-to-native dispatch surface |
| Modify | [`native_entries.py`](../../../scripts/windows/native_entries.py) | Certify the translated fragments and insert the existing native-background hook | Preserve `BLUEWAKE_NATIVE_ENTRIES=0`; reject a changed or watched translation |
| Modify | [`native_bg_test.c`](../../../tests/native_bg_test.c) | Compare native and translated execution over group trees, rejection paths and callbacks | Match CPU state and writable RAM byte-for-byte; prove native activation |

Implementation order:

1. Map every translated basic block, RAM field, call target, charged cycle and
   observable boundary for `0x802491F4–0x80249368`.
2. Extend the differential fixture first with shallow, recursive, filtered,
   callback-mutated, invalid-pointer, alias, deadline and interruption cases.
3. Implement an opt-in serial native path. Snapshot working values locally and
   commit guest-visible state only after all guards have passed.
4. Run translation/native differential tests, fallback tests, existing native
   suites and Linux/Windows CI. A passing serial fallback does not count as
   activation evidence.
5. Compare matched Deck A/B artifacts. Retain the path only if it activates in
   the target scene, reduces cost beyond noise and preserves checkpoints and
   gameplay.

The implementation may call existing native collision leaves only when their
combined register, timing and callback behavior matches the original call
sequence. It must not treat the decompilation as the execution oracle or assume
that every host hook is silent.

### Reimplementation impact register

| ID | Requirement | Baseline | Target and measurement | Risk | Owner |
|---|---|---|---|---|---|
| IMP-01 | FR-03, FR-05 | Translated routine; no native entry | Byte-identical CPU/RAM differential cases with positive native activation | Missed callback, timing boundary or write order | M1 |
| IMP-02 | FR-01, FR-02, FR-04 | 4.04–4.55% Game Mode main-thread self time; 28.25–28.65 FPS | Matched Deck A/B: cost reduction beyond noise and stable 30 FPS | Optimization is correct but insufficient | M0–M4, M9 |
| IMP-03 | FR-06 | Current default build recipe | Isolated compiler, tuning, native-entry and PGO comparisons | Overfitting or nonportable default | M5–M9 |

Critical path: M0 control → runtime M1–M4 and build-time M5–M8 → M9 combined
qualification. M1–M4 and M5–M8 are separate ordered tracks; they may run in
parallel, but each track tests only one new variable at a time.

## Implementation order

| Milestone | Depends | Deliverable / integration gate |
|---|---|---|
| M0 | Candidate ranking, #433 | Current-source control and A/A noise |
| M1 | M0 | Native collision candidate and retain/reject decision |
| M2 | M1 decision | Guest-alias candidate and retain/reject decision |
| M3 | M2 decision | Dispatch/numeric candidate and retain/reject decision |
| M4 | M3 decision | Conditional GX investigation if thread ownership changes |
| M5 | M0 | Clang 18/22 comparison; parallel with M1–M3 |
| M6 | M5 decision | Deck-specific tuning comparison |
| M7 | M6 decision | Certified-native comparison |
| M8 | M7 decision | PGO-weighting comparison and overfit check |
| M9 | M4, M8 | Combined retained candidates and final qualification |

### Candidate status after the scene sweep

`gGZLE01_recomp.so` entries execute in the loaded game module; `bluewake`
entries execute in the host. The headless scene profiles isolate the main
thread, while the earlier wall-paced rendered profile is process-wide. No
optimization target is selected yet.

| Candidate surface | Evidence across scenes | Decision |
|---|---|---|
| Ground-collision traversal in translated chunk [`func_802456E0`](../../../cmake/composite/module_export.c) | The chunk leads the Game Mode main thread at 4.04–4.55% and four headless scenes at 5.41–6.29%. Its leading annotated offsets map to `cBgW::GroundCrossGrpRp`; `func_803256E0` and `chassis_dispatch` also recur. | Selected bounded experiment: a semantics-matched native `GroundCrossGrpRp` path with translated fallback and differential tests. Do not parallelize its stateful recursion without separate proof. |
| Guest alias resolver in RecompCore [`cpu.c`](../../../ref/recompcore/GXRuntime/src/core/cpu.c) | `ppc_guest_alias_resolve` appears in every source-matched headless scene and reaches 4.13% in Forest Haven. | Second investigation. Annotate call sites and distinguish unavoidable aliases from fast-path misses before changing address resolution. |
| Native math/vector paths: [`native_game_math.c`](../../../cmake/composite/native_game_math.c), [`native_math.c`](../../../cmake/composite/native_math.c), [`native_vec.c`](../../../cmake/composite/native_vec.c) | Native game-math/vector probes consume roughly 2–3.5% in source-matched headless scenes and 2.10–2.54% in Game Mode. The worker probe's largest array was 8, below the 768-item parallel threshold, so `parallel=0`. | Third investigation. Measurable but not evidence for pool activation; find a genuinely large independent batch first. |
| GX draw-plan construction in [`gxcore.cpp`](../../../ref/recompcore/GXRuntime/graphics/gxcore/src/gxcore.cpp) | `build_draw_plan_into` is 4.10–4.49% in the source-matched Game Mode profiles. | Secondary render-thread cost, not the leading game-thread candidate. Keep separate from the 30 FPS critical path. |
| Host-specific helpers in [`main.c`](../../../runtime/host/src/main.c) | Corrected symbol-complete reports do not show actor search or another concrete host helper as a dominant recurring cost. | Do not select from the earlier unresolved reports. Reconsider only if Game Mode or annotated call paths expose a specific host cost. |

The current ranking is an investigation order, not M0 candidate selection.
Current-source control construction remains blocked by
[#433](https://github.com/chrissotraidis/bluewake/issues/433). The source-matched
Game Mode captures reproduce [#215](https://github.com/chrissotraidis/bluewake/issues/215)
at 28.25–28.65 FPS and rank the translated chunk surface, but the chunk must be
mapped and the result reproduced on a current-source control before selecting
a bounded candidate.
