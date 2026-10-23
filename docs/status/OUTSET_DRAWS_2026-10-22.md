# The draws at Outset, and the cheapest fusion left

Measured October 22, 2026, on a Steam Deck (VanGogh, 4 c / 8 t), the AppImage built from `main` at
`7382708`, rendered at `sea` room 44 from a save state, Smooth Motion on. Record: jkoehler11.

This is the groundwork for horizon 3 in [PERFORMANCE.md](../PERFORMANCE.md#the-plan) ("drawing at the
GX/J3D API level"), starting from the one frame the dump already gives us rather than from a rewrite.

## What a frame is made of

`DOL_GXCORE_DRAW_DUMP=110` lists every draw of one frame. That frame at Outset:

| | |
| --- | --- |
| draws | 662 |
| vertices | 40,512 (mean 61 a draw) |
| distinct pipeline signatures | 50 |
| runs of consecutive draws with the same state | 113 |
| runs of two or more | 51 |
| smallest draws | 465 draws of 4 vertices each |

Draw fusion (#225, patch 0166) has already taken the display lists from 8.8 M draws to 0.87 M over the
whole of Outset, and draw batching (0102) pairs what it can. What is left is 662 draws a frame, and two
thirds of them are 4-vertex quads: sprites, HUD pieces, particles, and the sea's strips.

## The runs worth fusing

The six largest runs of consecutive draws with identical state:

| Draws | Verts | Primitive | Transform | Note |
| --- | --- | --- | --- | --- |
| 142 | 568 | 0x80 quads | x 150675, z -332703 on every draw | same transform throughout |
| 84 | 336 | 0x80 quads | x 0, z 0 | same transform, two tags |
| 57 | 228 | 0x80 quads | x 150675, z -332703 on every draw | depth test off, same transform |
| 44 | 8,121 | 0x98 strips | x -39 to 312200, z -753 to 136414 | world geometry, spread out |
| 29 | 4,416 | 0x98 strips | x -23 to 60, z -350 to -240 | one model's parts |
| 25 | 120 | 0x80 quads | x 0 to 150675, z -332703 to 0 | same transform, tags vary |

Three of those (142, 57 and 25 draws) share their transform on every draw, so their vertex data could be
concatenated and submitted as one draw each without baking anything into the vertices: 224 draws become 3.
That is a third of the frame's draw count, from a change that does not touch the game's code and does not
need the vertex format or the shader constants to change.

The whole set of runs (113) is the upper bound if the transform were baked into the vertices, which is what
the HD project's batching does and what Dusklight-style particle batching would need; that is 662 draws down
to 113, an 83% cut, but it changes the vertex data and every draw's position matrix, so it is the second
step, not the first.

## Why the draw count is worth attacking on four cores

From the GX worker's profile at the same place ([PROFILING.md](../PROFILING.md)), the worker costs 35 to 43%
of one core and its three biggest self-time entries are per-draw work:

- `GxCoreState::build_draw_plan_into` 3.2%
- `ConsumingAuroraRenderSink::submit_packet` 2.0%
- `GxCoreSink::submit_packet` 1.7%

plus `RetailGxFrontend::emit_new_packets` 1.6% and `fill_render_packet` 1.5% on the way in. Fusing 34% of
the draws at this spot should take about a third off the plan-and-submit share, and it also cuts the GPU's
per-draw overhead (each 4-vertex quad is a draw call today).

## The change, concretely

In the GX core's plan builder, when a draw's pipeline key and its position-matrix rows are identical to the
plan already open, append its vertices to that plan instead of submitting another. The conditions are the
ones the dump shows directly: same pipeline signature, same transform, and nothing between them that
changes render state.

Two things to keep whole:

- **Draw order.** Concatenation preserves the order of the original draws, so the depth and blend results do
  not change. A fused plan must not reorder across a state change; the run boundary in the dump is exactly
  that invariant.
- **Smooth Motion's pairing.** The in-betweens pair draws by their key, and a fused draw is one key where
  there were several. The particle tags the interp path relies on (`draw_tag`) have to survive into the fused
  plan, or the sprites that pair by tag stop pairing.

## What to measure

Same five stops as the benchmark tour, headless and rendered, with `scripts/bench_tour.py` and one kept
state, against the build without the change:

- draws a frame at each stop (the dump gives this at any one frame; `[fps-dip]` reports `draws/frame`);
- the GX worker's share (`workers: gx=` in `[fps-dip]`, and `perf report --tid` for the worker);
- game frames a second, and the guest checkpoints identical (a drawing change must not touch guest state).

This record holds only the measurement and the shape of the change. The change itself belongs in
RecompCore; jkoehler11 cannot push there, so it is written up for whoever takes it.
