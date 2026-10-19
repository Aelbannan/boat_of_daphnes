# Watched-address lookup experiment

Supports M0-T02 and candidate selection for M2-T01; neither task is complete.

## Evidence

Desktop baseline: `3e82deb`, Clang 22, normal builder PGO and defaults.
Two rendered 30-second captures and one unpaced headless capture used the same
Outset state. All reported zero lost samples. The saved state's mod mask differs
from the active settings; these are exploratory desktop captures, not Deck qualification.

`chassis_dispatch` accounted for 2.74% and 3.20% of process cycles in the rendered
captures and 4.79% headless. About 29% of its headless samples landed at the first
watch-table load/test. Sampling skid prevents treating that instruction as an
exact cost attribution. Native-game-math wrapper symbols include inlined work.

The generated watch table contains 920 unique addresses in 4096 slots. Static
uniform-bucket miss analysis gives 1.326 probes on average and a maximum of 9.
This does not establish actual lookup frequency, locality or a cache hit rate.

## Measurement

`scripts/edge_watch_probe.py PATH/bw_edge_watch.inc` reports static table geometry.
Compile a diagnostic module with `-DBLUEWAKE_EDGE_WATCH_PROFILE=1` to count calls,
adjacent raw-address repeats, probes, and unwatched answers. Counters follow the
existing single game-thread ownership; the macro must be consistent across all
module translation units. Normal builds compile out all instrumentation.

Normal process exit prints `[edge-watch-profile]`. A crash or forced kill may
produce no summary; missing output is incomplete evidence. These aggregate counts
include startup and warmup, and instrumentation changes timing: use them for
locality analysis, never as the performance candidate in an A/B comparison.

The separate diagnostic build reuses the baseline generated sources, PGO profile,
compiler and CMake options, adding only the profiling macro. Outputs remain under
`build/desktop-edge-watch-profile/` beside the existing baseline build directory.
The baseline packaged binary is preserved.

## Desktop diagnostic result

The diagnostic module completed a headless run from `quick-7505.bwstate` to the
11105-retrace ceiling, exiting with status 0. The same mod mismatch remains.
Local raw evidence: `profiling/desktop-edge-watch-01/run.log` under the workspace's
BlueWake parent directory. The log confirms the diagnostic module was loaded.

| Counter | Observed |
|---|---:|
| Calls | 171,904,733 |
| Adjacent raw-address repeats | 5,875,921 (3.42%) |
| Table probes | 213,335,408 (1.24 per call, including range rejects) |
| Unwatched answers | 128,455,253 |

Decision: defer a one-entry last-address cache. Only 3.42% of calls repeat the
previous raw address, while its key comparison would affect every lookup. This
does not prove a cache is slower, or characterize multi-entry cache locality;
it provides insufficient support to prioritize that implementation. These are
whole-run counts including startup, not a timed A/B optimization result.

## Validation and decision

The direct-call C contract test passes with the macro disabled and enabled under
GCC 15.3.0, `-O2 -Wall -Wextra -Werror`. Its instrumented run reports 9 calls,
0 adjacent repeats, 4103 probes and 3 unwatched answers; the intentional full-table
case explains most probes. The six Python direct-call source tests also pass.

No result cache is implemented. First collect gameplay locality; only then consider
an opt-in cache with exact raw-address keys, initialization/lifetime tests, and
equivalence checks against the original lookup. A repeat counter alone does not
prove that the cache's branch/load overhead will pay off.
