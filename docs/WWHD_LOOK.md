# The Wind Waker HD look

BlueWake is rebuilding the look of *The Wind Waker HD* (Wii U, 2013) on top of the GameCube game, one piece at a
time. Each piece is an option, off by default, and one switch turns on every piece that exists so far:

- **Mac and Linux:** F1 › Display › **Wind Waker HD look**. It applies straight away.
- **Windows:** the settings menu › Display › **Wind Waker HD look**. It applies straight away.
- **iPhone and iPad:** Mods › **Wind Waker HD Look**, then restart BlueWake.
- **Android:** not yet (the port is in #93).
- **From a terminal:** `BLUEWAKE_HD_LOOK=1`.

Each piece also has its own variable, which wins over the switch (`=1` on, `=0` off), so a piece that costs too
much on a small GPU can be left out.

## Where this comes from

Elliott Tate showed most of the HD look running on the GameCube recompilation in a video on October 3, 2026
("I Backported All The Zelda Wind Waker HD Features to the Gamecube Recompile"). His code isn't in any public branch,
so we're rebuilding it from what the video shows, from the decompilation ([zeldaret/tww](https://github.com/zeldaret/tww))
and from a Wind Waker HD dump for reference. Nothing from the Wii U disc is copied into BlueWake: the HD textures
come from the player's own disc through [WWHD_TEXTURES.md](WWHD_TEXTURES.md).

## The pieces

| Piece | In the video | Status | Variable |
| --- | --- | --- | --- |
| HD textures and HUD | HD menus and HUD with Wii U button labels | Imported from your own Wii U dump ([WWHD_TEXTURES.md](WWHD_TEXTURES.md)); choosing it in the app's setup is next | `DOL_AURORA_TEXTURE_PACK` |
| Distant islands | Distant islands +100,000 units | **In.** Off by default | `BLUEWAKE_HD_DISTANT_ISLANDS` |
| Bloom | Bloom | **In**, with the switch (RecompCore 0169). Off by default | `BLUEWAKE_HD_BLOOM` |
| HD's far-distance blur | HD depth of field | **In**, with the switch (RecompCore 0170). Off by default | `BLUEWAKE_HD_FAR_BLUR` |
| Ambient occlusion | Ambient occlusion | **In**, with the switch (RecompCore 0171). Off by default | `BLUEWAKE_HD_AO` |
| Cloud shadows on the sea | Cloud shadows | **In**, with the switch, on the Great Sea (RecompCore 0172). Off by default | `BLUEWAKE_HD_CLOUD_SHADOWS` |
| Sun shadows | Sun shadows (resolution, strength) | **In**, with the switch, on the Great Sea, from what is on screen (RecompCore 0173). Off by default | `BLUEWAKE_HD_SUN_SHADOWS` |
| Edge light | Character edge light | **In**, with the switch, on the Great Sea (RecompCore 0174). Off by default | `BLUEWAKE_HD_EDGE_LIGHT` |
| Torch light | Torch and fire light | Planned | |

### Distant islands

Every island on the Great Sea has a simple far-away model (the `d_a_lod_bg` actor, process name `0x1C3`). The game
shows it while you're within the actor's `scale.x` (its placed scale times 20,000 units: 150,000 for most islands,
250,000 to 350,000 for landmarks such as Tower of the Gods) and unloads it 20,000 units past that. HD draws them
further out. With the option on, the host adds 100,000 units to both distances on every island model once a frame
(`runtime/host/src/hd_look.c`). It changes only those two numbers in the actor, so the game loads, fades and draws
the models with its own code, and turning the option off puts the numbers back.

The game also lowers a far-away model below the sea by a tenth of its distance past 150,000 units, so islands that
are now visible sit low on the horizon and rise as you sail closer, like the curve of the earth. That part is kept.

Checked on an M3 Max from an Outset save state: 15 island models loaded instead of 7, a second island on the
horizon in a captured frame, no lookup failures. `BLUEWAKE_HD_TRACE=1` logs each island model it changes and,
every 300 retraces, the ones within reach with their distance and whether they're loaded.

Costs: more island models in memory at once (the game's own small models, loaded from `LODALL.arc` in ARAM) and a
walk over the actor list once a frame while the option is on. Nothing runs while it's off.

### Bloom

Wind Waker draws its 3D scene, copies the finished scene out at full size, and then draws the HUD flat on top. Bloom
runs in Aurora just before that copy: whatever is brighter than a threshold is blurred at a quarter of the size and
added back, so clouds, foam and white highlights glow while the HUD stays sharp. It's six small GPU passes a frame
and nothing while it's off. `DOL_AURORA_BLOOM_THRESHOLD` (default 0.72) and `DOL_AURORA_BLOOM_STRENGTH` (default
0.35) tune it; `DOL_AURORA_PASS_TRACE=1` logs one frame's passes.

Checked on an M3 Max from an Outset save state: the clouds glow and the HUD is pixel for pixel the same as without
bloom; Smooth Motion runs with it. Not yet on Windows, Linux, a Deck or a phone.

### Far blur

HD softens the distance. At the same point as bloom, the scene is blurred at a quarter of the size and blended back
by distance, before bloom. Aurora's depth gives the distance; Wind Waker draws the sky, the sea toward the horizon and
the islands' far models without depth, so those get a lighter amount of their own. At Outset's pier, Link is about
240 near-plane units away and the island's cliffs about 3,700, so the blur starts at 8,000 and is full at 30,000:
the island stays sharp and the horizon softens. `DOL_AURORA_FAR_BLUR_START`, `_END`, `_AMOUNT` (0.8) and
`_BACKGROUND` (0.35) tune it, and `DOL_AURORA_FAR_BLUR_DEBUG=1` shows the distance as brightness. It needs the
renderer's full feature set, so it stays off in compatibility mode.

Checked on an M3 Max from an Outset save state: the horizon, far islands and sky soften, the island and the HUD stay
sharp. The switch alone gives the same frame as turning on each part by hand.

### Ambient occlusion

HD darkens creases and the ground where things meet it. Before the far blur and bloom, from the depth buffer alone, at
a quarter of the size: around each pixel, six pairs of samples on opposite sides, in a ring that shrinks with
distance. A pair whose average is nearer than the pixel (a crease, a contact) darkens it; on a flat floor the two
samples straddle the pixel, so it stays as it was. Where there is no depth (the sky, the horizon) and across big
steps (a silhouette in front of the background) nothing is darkened. `DOL_AURORA_AO_RADIUS` (default 30) and
`_STRENGTH` (0.6) tune it, and `DOL_AURORA_AO_DEBUG=1` shows the occlusion alone.

Checked on an M3 Max from an Outset save state: darker under Link's feet, round the pier's posts and in the cliffs'
creases; the pier's deck and the sea unchanged.

### Cloud shadows

Soft shadows drift over the sea and the islands, as in HD. On the Great Sea, the host reads the game's camera once a
retrace (the current view's inverse view matrix, field of view, aspect and near plane) and passes it to Aurora. After
ambient occlusion, each pixel's place in the world comes from that camera and the scene's depth, or, below the horizon
where there is no depth, from where the view ray meets the sea. Smooth noise in world X/Z, drifting slowly, darkens it
and fades out with distance. `DOL_AURORA_CLOUD_SHADOWS_STRENGTH` (0.28) and `_SCALE` (world units across a cloud,
7,000) tune it; `DOL_AURORA_CLOUD_SHADOWS_DEBUG=1` paints a 1,000-unit world grid to check the camera.

Checked on an M3 Max from an Outset save state: the grid sits square on the pier and the cliffs, and the shadows show
on the open sea and the cove. Because the camera is read once a retrace, the pattern may trail by a frame in a fast
turn; not checked by hand.

### Sun shadows

HD casts sun shadows across the whole scene. On the Great Sea, the host also passes the direction to the sun
(`g_env_light.mSunPos`, which the game keeps at the camera's eye plus that direction). After ambient occlusion, from each
pixel's place in view space, 24 steps go toward the sun; a step that lands behind a surface in the depth buffer is in
shadow. It's drawn at a quarter of the size, blurred and multiplied in, and fades with distance and when the sun is low.
Because it works from the screen, something off screen casts nothing; a full shadow map, the scene drawn again from the
sun, would be the next step. `DOL_AURORA_SUN_SHADOWS_STRENGTH` (0.35) and `_REACH` (world units, 1,500) tune it.

Checked on an M3 Max from an Outset save state with the sun high: Link casts a soft shadow on the pier away from the
sun, and the ladder's post and the pier's edges darken; the HUD is unchanged.

### Edge light

HD rims characters with light on their sunlit side. After sun shadows, for anything within 1,500 units of the camera,
the renderer looks a few pixels toward the sun on screen: where the depth there jumps far behind or there is none, the
pixel is on a sunlit outline and gets a warm rim. It isn't limited to characters; anything near with sky or something
far behind it on the sun's side gets it. `DOL_AURORA_EDGE_LIGHT_STRENGTH` (0.4) tunes it.

Checked on an M3 Max from an Outset save state: Link's arm, sleeve and shirt on the sun's side, and the ladder's posts,
get a warm rim; the HUD is unchanged.

