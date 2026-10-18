# The HD look's sun shadow map: how it works and how to keep going

Work in progress (RecompCore#42, BlueWake#398). This page is for whoever picks it up next: what it does, where the
code is, how it was tested, and what's left. [WWHD_LOOK.md](WWHD_LOOK.md) has the player-facing description.

## The problem it solves

The HD look's first sun shadows (RecompCore 0173) were screen-space: for each pixel the shader stepped toward the sun
through the frame's depth buffer. A step that left the screen counted as lit, so anything off screen cast nothing.
pdale-boop's report: "objects that cast shadows have no permanence. As soon as something is off screen, even walking
between buildings, suddenly you're in light again." On open ground the screen version also drew long dark streaks,
which is why #321 took sun shadows out of the HD look switch.

Two things could hide a caster: the screen-space method, and the game culling what the camera can't see. A headless
sweep of Windfall's 24 spawn points (counting what `J3DUClipper::clipByBox` hides) found the room model hides only 0
to 11 of its 42 shapes, usually 2 to 7, because it culls by large joint boxes. So the buildings are nearly always
drawn, and the screen-space method was the main cause. That's why the shadow map came first.

## How it works

Turned on by `BLUEWAKE_HD_SUN_SHADOWS=1` (BlueWake's `runtime/host/src/hd_look.c` calls
`aurora_set_sun_shadow_map(true)` with it). `DOL_AURORA_SUN_SHADOW_MAP=0` keeps the old screen-only shadows.

1. **The sun and camera.** Every retrace, `hd_look.c` `clouds()` reads the game's sun position and camera and hands
   them to Aurora (`aurora_set_sun_direction`, `aurora_set_world_view`).
2. **Drawing the map.** Before each EFB pass, `common.cpp` `encode_sun_map()` draws that pass's casters again from
   the sun into a 2048×2048 depth map (the frame's first pass clears it). Every gxcore draw already keeps its full
   transform state in its constants, which is what Smooth Motion relies on to re-encode a frame, so one matrix,
   `sunOrtho × sunView × cameraInvView`, takes each draw from the camera's view space into the map. A few fixed
   pipelines (`gxcore_draw.cpp` `make_sun_caster_pipelines()`: current matrix or indexed positions, with or without a
   texture for cutouts) draw them all with each draw's own constants, so nothing is compiled per draw. The map
   covers 5000 world units around a point ahead of the camera and moves by whole texels so its edges don't crawl.
   In an in-between frame the draws use their blended constants and that frame's camera.
3. **Which draws cast** (`gxcore_draw.cpp`):
   - `sun_caster()`: writes depth and colour, not blended, triangles, and its alpha test keeps every pixel. Scenery
     and characters.
   - `sun_caster_alpha()`: the same but its alpha test drops pixels (leaves, fences, roofs). Its pipeline samples
     the texture and discards below half, so it casts its cutout.
   - Cloth: a scope over draws that index their positions (the sail, flags) casts solid whenever it writes depth,
     even though the game draws it blended.
   - Blended furniture: a blended, depth-writing, indexed draw whose positions stood still since the frame before
     (a chest, Beedle's ship, a Wizrobe). The sea is excluded because it rewrites its grid every frame.
   - Left out: what's blended otherwise (water, clouds, particles), what writes no depth (the sky), lines and points.
4. **The game's own shadows are left out** while the map is on, so they don't darken things a second time. Its
   projected shadows are recognised by their blended re-draw of the ground, and its blob and volume shadows by the two
   fixed position arrays they index (`d_drawlist`'s `l_shadowVolPos` 0x8037C554 and `l_simpleShadowPos` 0x8037C7DC).
   `DOL_AURORA_GAME_SHADOWS=1` keeps them.
5. **Looking it up** (`post_fx.cpp`). The sun shadow pass takes each pixel's world position into the map and filters
   12 lookups in a disc two texels wide, turned per pixel by a stable angle, so the edge is about ten units soft and the
   texels break into fine noise instead of stairs. The result is multiplied into the scene at full resolution. The
   old screen steps are off (`DOL_AURORA_SUN_SHADOW_MAP_STEPS=1` brings them back for what the map leaves out); when
   on, a step counts only where the map doesn't already hold that surface, which stops a second shadow of Link
   appearing behind him.

## Switches

| Switch | Default | What it does |
|---|---|---|
| `BLUEWAKE_HD_SUN_SHADOWS` | off | Sun shadows on, using the map |
| `DOL_AURORA_SUN_SHADOW_MAP` | follows the above | `0` keeps the screen-only version, for A/B |
| `DOL_AURORA_SUN_SHADOW_MAP_SIZE` | 2048 | Texels a side |
| `DOL_AURORA_SUN_SHADOW_MAP_RANGE` | 5000 | World units from the map's centre to its edge |
| `DOL_AURORA_SUN_SHADOW_MAP_DEPTH` | 20000 | World units toward and away from the sun it holds |
| `DOL_AURORA_SUN_SHADOW_MAP_BIAS` | 12 | World units a surface moves toward the sun before the lookup (raise it for acne, lower it for gaps at feet) |
| `DOL_AURORA_SUN_SHADOW_MAP_CONTACT` | 300 | Reach of the screen steps, when they're on |
| `DOL_AURORA_SUN_SHADOW_MAP_STEPS` | off | Bring back the screen steps |
| `DOL_AURORA_SUN_SHADOWS_STRENGTH` | 0.35 | How dark the shadow is |
| `DOL_AURORA_GAME_SHADOWS` | off with the map | `1` keeps the game's own shadows |
| `DOL_AURORA_SUN_SHADOW_MAP_DEBUG` | off | `1` shows the shadow term alone (gray outside the map), `2` the map itself as contour bands |
| `DOL_AURORA_SUN_SHADOW_MAP_TRACE` | off | Logs how many draws the map took |

Debug only, to be removed before review (they're separate commits in #42): `DOL_AURORA_SUN_SHADOW_MAP_ALL=1` lets
alpha-tested draws cast solid (to see which shadows a rule is costing), `DOL_AURORA_SUN_CASTER_LOG=1` says why each
scope's draws did or didn't cast, and `DOL_AURORA_FRAME_INTERP_DUMP_KEY=N` makes F3 dump the next N game frames
(Windows only, for now).

## How it was worked on

The loop was short: change RecompCore, build host-only, play, look, repeat.

- **Host-only builds.** All of this is renderer code, so `--host-only` over an existing trained build is enough and
  takes seconds to minutes. Point the builder at a local RecompCore checkout to skip pushing between tries.
- **Playing by hand first.** pdale-boop played each step and described what looked wrong: "Link's shadow is a blob",
  "the sail casts nothing", "a dark patch at his feet inside a building's shade". Each of those became one commit.
  Save states (`BLUEWAKE_LOAD_STATE=<file>.bwstate`) pinned the places where something looked off, so every try
  started from the same frame. Renderer switches like the ones above do apply after a state load.
- **Fixed time of day.** The sun moves, so shadows move. On islands the game's clock holds between 11:00 and 18:00
  (and 23:00 to 6:00), so warp there at a fixed hour: `BLUEWAKE_WARP=sea:11:0 BLUEWAKE_WARP_TIME=16` puts you at
  Windfall at 4 pm ([STAGE_SELECT.md](STAGE_SELECT.md)). A state saved before 11:00 runs forward to 11:00 and then
  holds, which looks like shadows drifting but is the real sunrise.
- **A/B in one build.** `DOL_AURORA_SUN_SHADOW_MAP=0` against the default, and `DOL_AURORA_GAME_SHADOWS=1`
  against the default, separate the map's shadows from the old ones and from the game's.
- **The debug views.** `_DEBUG=1` shows what the map darkens and nothing else; `_DEBUG=2` shows the map's depth as
  bands, which makes a missing caster obvious (no bump where the object is). The caster log names the rule that
  refused a draw.
- **Frame dumps.** `DOL_AURORA_FRAME_INTERP_DUMP=<dir>` with `_FROM`/`_TO` writes every real and in-between frame as
  images, so a flicker can be checked frame by frame. They need a paced run (not `BLUEWAKE_WALL_PACE=0`), and they're
  big: at render scale 2, 90 frames come to about 13 GB.
- **Scripted warps for numbers.** Headless stage-select sweeps measured the culling, and rendered warps measured the
  map's cost: 0.04 to 0.07 ms of GPU time a frame on an RTX 5070 Ti (the first version; not remeasured since).

## What's left, with leads

- **A blue Chuchu's oval.** The game draws it itself (it stays with the HD look off) and it doesn't go through
  either of the paths the map skips (`DOL_AURORA_GAME_SHADOWS` on or off makes no difference). The goal is no
  GameCube-style darkening next to the HD shadows: find what makes that draw distinct (a frame-interp trace on a state
  with a Chuchu) and leave it out while the map is on.
- **Moon shadows at night.** When the sun is below the horizon, cast from the moon instead: the decomp puts the moon
  at the eye minus the sun's offset (`mMoonPos`), at a lower strength.
- **Casters past 5000 units.** A second, wider map (a far cascade) for distant islands and tall casters. Until then,
  `_RANGE` and `_SIZE` trade reach for sharpness.
- **Shadows popping when an actor leaves the view.** The game stops drawing a culled actor, so it leaves the map.
  Buildings are rarely culled; actors are. The fix is on the game side: widen the clipper while the room model and
  then actors draw. Actors with `fopAcStts_NOCULLEXEC` stop updating when they aren't drawn, so widening their culling
  changes what runs off screen; keep that behind its own switch and measure the CPU cost. Walking NPCs already turn the
  flag off themselves.
- **Surfaces facing away from the sun** are shaded by the map, which the screen version never did. It looks right
  to us, but it's a change in the look worth a second opinion.
- **Before review:** drop the four debug commits from #42, regenerate patch 0183, and decide whether
  `DOL_AURORA_SUN_SHADOW_MAP=0` stays as a user-facing comparison switch.
- **Platforms:** played on Windows (Direct3D 12) and Linux (Vulkan), both RTX 5070 Ti. Not yet on AMD or Intel GPUs,
  Mac (Metal) or iPad.

## Code map

RecompCore, `GXRuntime/graphics/aurora/`:

- `include/aurora/aurora.h`: `aurora_set_sun_shadow_map()`.
- `lib/gfx/post_fx.cpp`: the map's settings and texture, `sun_map()` (its matrices), `draw_sun_map()`, the lookup
  shader (`in_sun_map`, `fs_sun_map`) and the debug views.
- `lib/gfx/gxcore_draw.cpp`: the caster rules (`sun_caster`, `sun_caster_alpha`, cloth, blended furniture), the
  caster pipelines, `begin_sun_casters()` / `render_sun_caster()`, and the game-shadow skips.
- `lib/gfx/common.cpp`: `encode_sun_map()`, which runs the casters before each EFB pass.
- `lib/gfx/frame_interp.cpp`: `collect_parts()` is shared with the map, so in-between frames cast from their blended
  constants.

BlueWake: `runtime/host/src/hd_look.c` (the switch, sun and camera), `patches/recompcore/0183-*.patch` (the RecompCore
change as one patch), `config/dependencies.lock.json` and `scripts/builder/profiles/bluewake.sh` (the pin).

To move the pin after changing RecompCore: update `RECOMPCORE_SHA` in the profile and the `sha` and
`sun_shadow_map.runtime` entries in the lock, regenerate 0183 from #40's head to the new commit, then run
`scripts/audit_repo.sh` and `python3 scripts/check_attribution.py origin/main..HEAD`.
