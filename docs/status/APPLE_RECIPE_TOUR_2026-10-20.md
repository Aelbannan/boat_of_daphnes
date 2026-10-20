# The Windows and Linux recipe on the Mac, on the benchmark tour (October 20, 2026)

The question: should Mac (and then iPhone and iPad) builds use the module preparations and behaviour timing that
the Windows and Linux builders have used by default since #277 and #335? Until now they are opt-in on Apple.

## How it was measured

- Both builds from `f2611b1` (0.8.0), the same disc (identical `main.dol`, composite digest `54f54434…`), local
  training, `scripts/builder/build.sh DISC --platform macos --jobs 8`, on an M3 Max.
- **Default:** the 0.8.0 candidate's Mac build from October 17.
- **Recipe:** the same, plus `--combined-optimizations --lean-blocks --lean-memory --lean-deadlines --behaviour-timing`.
  Its log: 461,975 lean block copies, 526,629 lean memory accesses, 589,378 lean deadline tests, and 401,618 block
  starts always prepaid.
- `scripts/bench_tour.py` from a copy of a card, each run in its own home, user and cache folders. Headless: three
  pairs, alternating default and recipe. Rendered: one default run, two recipe runs. Desktop apps were open (load
  average 4 to 6). The card was unchanged afterwards.

## Results

Headless game frames a second (medians of three), and the game thread's busy share in the rendered runs, where both
hold 30 game frames a second:

| Stop | Default | Recipe | Change | Rendered busy, default | Rendered busy, recipe (two runs) |
| --- | --- | --- | --- | --- | --- |
| Outset | 47.4 | 77.9 | +64% | 65% | 43% / 43% |
| Windfall | 48.6 | 82.2 | +69% | 64% | 41% / 40% |
| Dragon Roost Cavern | 64.2 | 113.7 | +77% | 53% | 33% / 35% |
| Forest Haven | 38.5 | 65.7 | +71% | 83% | 50% / 47% |
| sea by the Fortress | 35.3 | 59.8 | +69% | 89% | 53% / 51% |

Every pair was 64% to 79% faster at every stop. Each build repeats itself: two runs of one build are within 1.5% with
matching checkpoints. The recipe's game module is 447 MB instead of 527 MB, and it compiled in 24 minutes instead of 90.

The two builds' checkpoints differ from the first stop. That is expected: behaviour timing lets a timer land up to a
block later, so the game state isn't cycle-identical (see the October 12
[behaviour timing record](BEHAVIOUR_TIMING_2026-10-12.md#results): the same 468-place sweep outcomes and identical
audio). One rendered recipe run logged 30 checkpoints instead of 31 (8100 missing); the repeat logged all 31.

This run doesn't separate the preparations from behaviour timing. Earlier M3 Max rows in
[PERFORMANCE.md](../PERFORMANCE.md#results) put behaviour timing at 7 to 9% on top of the lean preparations, so most
of the gain is the preparations.

## Not checked yet

- Play by hand on the Mac: a cutscene with music, a save and load, the menus, Picto Box, the HD look.
- An iPhone or iPad build with the same options, and the same tour there. The earlier M2 iPad Pro comparison
  (combined preparations) took Outset from 81% to 54% game-thread busy.
- The 468-place sweep on this build.
