# Game-behaviour timing (T1): what's left to take out

October 12, 2026. A design note for the prototype in [GOAL_LOOP.md](../GOAL_LOOP.md) (T1), from the prepared source of
an untrained lean-deadlines Mac build. Nothing here is built yet.

## Where a block's time goes now

With lean blocks, lean memory and lean deadlines (on by default in the Windows, Linux and Android builders), every
block has two copies. The fast copy (`bwfast_N`) runs the block's instructions with no per-instruction bookkeeping;
the precise copy (a `ctx->pc` store and `dolrecomp_charge_precise` before each instruction) only runs when the
cycle budget is nearly spent. What every block still pays on the fast path:

- `ctx->pc` stored at the block's start;
- `dolrecomp_block_can_precharge`, the turn's cycle budget compared, `ctx->downcount` decremented;
- the guest's registers in `ctx->gpr[]`; with `BLUEWAKE_FIXED_CPU` the CPU state and guest RAM are separate
  globals, so the compiler already keeps most of them in host registers inside a block (the busiest chunk compiled
  for the Mac: about 6,400 32-bit loads and 9,300 stores at an offset in 233,000 lines of assembly, many of them
  the `ctx->pc` store at offset 640);
- at each direct call, `bw_direct_call_ready` before the call and on the return (inlined at 500,773 sites; moving it
  out of line was no faster on the M3 Max, see [SPEED_PROFILE](SPEED_PROFILE_2026-10-11.md)).

## What "behaviour timing" would change

The HD recomp reproduces what the game does each frame, not how many cycles the console took, which is where its
speed comes from. The equivalent here, behind a translator switch:

1. **Cycles charged per block from a table, checked only at loop back-edges and calls.** Today's per-block budget check
   becomes a check where control can loop. The decrementer, the audio DMA and VI keep their own time from the
   charged cycles, so the game's clock still advances; it advances in larger steps.
2. **Guest registers kept in host registers across blocks,** not only inside one: the compiler already does it
   inside a block (see above), so what's left is the write-back and reload at every block boundary and call. Needs
   the translator to know which instructions can leave the block.
3. **No `ctx->pc` store on the fast path;** the exit sets it.

## How it would be judged

The same as every speed step: headless from save states at Outset, the bird scene and Dragon Roost Cavern, guest
checkpoints compared, then by hand. Unlike the earlier steps the checkpoints *will* differ (the clock moves in larger
steps), so the bar is that the game behaves the same: the 468-place stage select sweep, cutscenes with their sound in
time, the audio stream without starving, a save made and loaded. Build time measured too: fewer copies of each block
is also what a first-start build in minutes needs ([DIRECTION.md](../DIRECTION.md)).

## Where it lives

DolRecomp (the translator, a submodule of RecompCore) and the composite's preparation scripts. A prototype is a
DolRecomp branch plus a `--behaviour-timing` builder option, off by default; Chris decides any default.

## Results

Step 1 is built: `scripts/windows/behaviour_timing.py`, `--behaviour-timing` in the Windows, Linux and Mac builders,
off by default. It rewrites each block start that has a prepaid copy (384,783 of them) so the block always runs that
copy and is charged its cycles at once; the turn's budget is still checked at every block, and the program counter is
stored only on that way out. Points 2 and 3 above (back-edge checks, registers across blocks) are not in it.

M3 Max, untrained modules from the same prepared source (lean blocks, memory and deadlines), the same app, alternated,
three rounds of 2,400 retraces from save states:

| | Without | With | Change |
| --- | --- | --- | --- |
| Outset (s) | 25.79, 25.97, 26.10 | 27.74, 24.48, 24.18 | 5.7% faster (medians) |
| Dragon Roost Cavern (s) | 15.17, 15.18, 15.28 | 14.05, 14.07, 14.11 | 7.3% faster |
| Blocks run | 1,237,187 / 1,250,840 | 1,237,192 / 1,250,836 | |
| Module | 418.1 MB | 391.7 MB | 6.3% smaller |
| Module compile (CPU time) | 15,286 s | 11,061 s | 28% less |

The game state is not identical, by design: an event can land a block later. What it should keep:

- The 468-place stage select sweep from a Dragon Roost save: the same outcome at every place on both (446 arrive,
  the same 11 stop at the retrace limit and the same 11 known crashes), and the card unchanged after all 468.
- The sound: the audio the game produces over each 2,400-retrace run, captured with `BLUEWAKE_CAPTURE_AUDIO_WAV`, is
  identical sample for sample at both places (1,279,544 and 1,279,736 samples).

Not checked: played by hand, a PC, the Deck, a phone, a trained build, and long runs (cutscenes with their sound in
time, a save made and loaded).

