# Windows tasks

Work that needs a Windows PC. Elliott owns Windows; anyone with the hardware can help. Read
[AGENTS.md](../AGENTS.md) first. One pull request per task, results in the linked issue.

**You need:** Windows 10 or 11 (x64), a CPU with AVX2, a Direct3D 12 GPU, your own USA `GZLE01` revision 0
disc, and the build tools in [BlueWake on Windows](WINDOWS.md#what-you-need).

**Where things stand (October 11, 2026):** 0.7.0 is the Windows download, built on Chris's PC (Ryzen 7 5700U)
from the release commit: a 27-minute title session with draw fusion on, D3D12 and no fatal lines, but nothing
played by hand ([Windows build for 0.7.0](status/WINDOWS_BUILD_0.7.0.md)). pdale-boop checked 0.6.0 on a second
PC (i5-12600KF, RTX 5070 Ti) by ear and with controllers ([his checks](status/WINDOWS_CHECKS_0.6.0_2026-10-07.md)).
The 0.5.0 checks are in [Windows build for 0.5.0](status/WINDOWS_BUILD_0.5.0.md).

## 1. In `main`, for the next Windows build

| Change | Issue | What to check | Result |
| --- | --- | --- | --- |
| Developers' stage select toggle | #210 | F1 › Mods › Developers' stage select (F7), then restart: holding R while starting a file opens the stage select with English names, and F7 opens it from play. | pdale-boop played it on Windows before merging; not checked on a release build |
| Option: Swap L and R (the triggers) | #244 | F1 › Controls › “Swap L and R (the triggers)” on: pulling the left trigger raises the shield (R) and the right trigger targets (L), with any custom button layout kept. Restart: it is kept (`controller_swap_lr=1` in `settings.ini`). Off by default. | CI test on Windows and Linux; not pressed on a controller |
| On by default since October 12 (Chris's call): lean memory and lean deadlines in the builder, and the lazy overlap watch in the app (the host stops reading the scene's overlap phase at every direct call, and the game module answers the per-call question itself) | #59, #215, #93 | Build from `main` with no options; the build log runs `lean-memory` and `lean-deadlines`, and the session log says `lazy overlap watch: on` and `inline-answer=on`. Play through a door, a cutscene and a load from the sea; nothing should look different. Retraces a second against `$env:BLUEWAKE_LAZY_OVERLAP="0"` at the same spot, and if there is time a build with `--no-lean-memory` | Mac: 13 to 16.5% less game-thread time together, identical checkpoints |
| The Wind Waker HD look: islands on the horizon from further away, a soft distance and bloom (off by default) | #57 | Settings › Display › "Wind Waker HD look" on. Clouds and white highlights glow and the horizon is softer, with the HUD sharp. Stand at Outset's pier facing open sea, or sail: more islands on the horizon, low at first and rising as you get closer. The log says `[hd-look] distant islands on`. Turn it off and on again in the menu without a restart; restart and it's kept (`hd_look=1` in `settings.ini`). Play a few minutes on the sea: no missing islands close up and no crash. | Mac: 15 island models loaded instead of 7 from an Outset save state, an extra island in a captured frame; not built on Windows |
| The host's per-block checks do less (#242) | #59, #246 | Nothing new to press: the game plays as before. If you benchmark, compare retraces a second with 0.7.0 at the same save state. | Measured on Linux (5 to 8% on slow CPUs, identical game state); not measured on Windows |
| Stage select turns saving off until a restart; F7 asks first; marked cutscenes get their half of the story; `--stage-select-saves` keeps saving on | #251, #250 | With a copy of a saved card and the stage select on: F7 in play shows "Go to the stage select?". Cancel (Esc or B) keeps playing; Go (or F7) opens it. Warp, then pause › Save: "Saving is off because the stage select was used." Quit and confirm the card is unchanged. F7 during a conversation, before warping, does nothing; after warping, F7 leaves a cutscene stuck at its end (Storm Ganon part 2). Pick a (after) cutscene from a before-the-fight file: it plays. A save state (F5) taken after warping, loaded after a restart, still can't save. With `--stage-select-saves`, F7 doesn't ask and saving works. | Played on Windows (RTX 5070 Ti), test card: the question cancels and goes, saving stays off and the card unchanged, the event check holds before warping and F7 leaves Storm Ganon part 2 after, marked cutscenes get their half, the game saves with the flag, the reworded Save message shows, and an F5 state taken after warping, loaded with F8 after a restart, still can't save (card unchanged). Headless: no speed change. |

## 2. Shipped, still to check by hand

| Change | Issue | What to check | Shipped in | Result |
| --- | --- | --- | --- | --- |
| Draw fusion (Elliott Tate, RecompCore patch 0166): a tenth of the draws, on for Windows | #86, #137, #59 | Play Outset, Forest Haven and Dragon Roost with Smooth Motion off, then at 60. The log says `[gx] draw fusion on` and, at exit, the draws fused. Look for missing or flickering geometry, wrong colors on models, and broken water or particles. Compare frame rates with `DOL_GX_FUSE=0`. | 0.7.0 | On in the release build; Forest Haven, Dragon Roost and the `DOL_GX_FUSE=0` comparison not played. Played clean on Linux (#215) |
| Smooth Motion keeps its in-between frames when the game runs slow, on 8 threads or more (patch 0164); the log names the renderer (0163) | #137, #56 | Smooth Motion on at 120 (F1 › Display). Play Outset and the bird scene for two minutes. The log says `[interp-pace] N threads: a slow game keeps its in-between frames` and `[renderer] D3D12` (or Vulkan); there should be no `[interp-pace] ... the game ran below full speed` lines, and the picture shouldn't step down to 30 for seconds after a hitch. | 0.7.0 | Not played |
| Player 1 follows the controller you press while player 1 is idle; rumble only on player 1 | #155, #190 | A GameCube adapter in PC mode with the controller in port 2: press A at the title; the log says `is player 1 (pressed while player 1 was idle)` and the controller plays. Two controllers: rumble (the Forsaken Fortress launch, or any hit) shakes only the one playing. | 0.7.0 | CI test; the Mayflash adapter confirmed by its reporter (#155); rumble not checked |
| Pictobox: the left stick picks Yes or No at "keep this picture?" and moves through the gallery, with the fast right-stick camera on | #191, #13 | Take a photo with a controller; at "keep this picture?" push the left stick left and right and choose Yes; open the gallery and move through it with the left stick. Then in the live view, push the left stick up and down: it still zooms. | 0.7.0 | Not checked in game |
| Optional controller zoom with the right-stick click | #203 | F1 › Controls: enable controller zoom. In the normal follow camera, hold the right-stick click and move the stick down to move the camera farther away, then up to bring it closer. Release after zooming: first person must not open. Tap the click without moving the stick: first person must still open and close. Change the speed, restart, and confirm both settings were kept. Telescope, Picto Box, targeting, sailing and cutscenes must retain their existing controls. | 0.7.0 | Linux build and gesture test pass; not checked on Windows |
| Portable mode keeps controller remaps, keyboard bindings and `imgui.ini` in the `user` folder (RecompCore patch 0160) | #64 | With `portable.txt` beside `BlueWake.exe`: remap a controller button and change a keyboard key, quit, start again. The remaps are kept, the `user` folder has `imgui.ini` and a `*.controller` file, and no new `imgui.ini` or `*.controller` appears in `%APPDATA%\BlueWake`. If `%APPDATA%\BlueWake` already had remaps, the first portable launch logs `[portable] copied ...` and keeps them. Without `portable.txt`, remaps made before still work. | 0.7.0 | Not checked |
| The window opens in place: centred the first time, then where you left it, without appearing first and then jumping (RecompCore patch 0161) | #89 | Start BlueWake windowed: the window appears once, centred. Move it, quit, start again: it opens where you left it. Move it to a second monitor, quit, unplug that monitor, start: it opens centred, and the log says `[windows] window centred: ... is no longer on a monitor`. Fullscreen still starts fullscreen on its display. | 0.7.0 | Not checked |
| Opt-in cache-flush fallback for stale Pictobox preview | #13 | Set `$env:BLUEWAKE_CACHE_FLUSH_FALLBACK="1"` and use a copied save. Take a photo, cancel, turn toward a different view and take another without changing rooms. Both regular and Deluxe previews must show the new view; save/reload one photo. Check left-stick selection separately. For a short diagnostic only, set `DOL_GXCORE_TEX_VERIFY=1` and retain the log. Remove both variables afterward. | 0.6.0 | Off by default; Windows untested |
| The builder works with Visual Studio 2022's clang | #153 | Build from `main` with Visual Studio 2022 (clang 19): the output says `note: building the app without its optimization profile (... unsupported instrumentation profile format version)` and the build finishes. With Visual Studio 2026 18.10 or newer there is no note. | 0.6.0 | Checked on a Mac with LLVM 18, 20 and 22; Windows untested |

Still open from 0.5.0: the camera-stick invert, "Quit the game" and the option notes, and the mouse buttons, by
hand ([details](status/WINDOWS_BUILD_0.5.0.md)).

## 3. Checked on Windows

- 0.6.0, by pdale-boop: the history intro's music (#97, deferred DVD completion, on by default on Windows), the
  dungeon map in Dragon Roost Cavern (#74, also confirmed by its reporter), the baton (#156), the aiming option
  (#154), a late-recognized controller as player 1 (#61) and the left stick's dead zone (#138). The last two
  needed the controller plugged in after launch; 0.7.0 fixes the launch case, and #138's reporter hasn't
  confirmed it yet.
- 0.5.0: Exact sound, safe mode after a crash, the AVX2 message under Intel SDE (#77), keyboard keys,
  `gamecontrollerdb.txt`, the `.rvz` prompt, portable caches and Molgera's floor (#126).

## 4. Flickering clouds, waves and fog with Smooth Motion (#136)

Reported on NVIDIA (RTX 5070 Ti, RTX 4070 laptop), new in 0.5.0, still in 0.7.0. Clean at 30 FPS, so it is in
the in-between frames. Ruled out by the reporters: `DOL_AURORA_INTERP_ALL_VERTICES=1` (patch 0155),
`DOL_GX_TRANSFORM_VERIFY=1` (0152), `DOL_AURORA_UBERSHADER=0`, the HD texture pack and G-Sync. It hasn't
been seen on the Mac. Next: see whether it shows on Chris's PC (AMD graphics) at 120, then capture it on an
NVIDIA PC:

1. Record roughly ten seconds at original 30 FPS, then the same view at Smooth Motion 60 and 120. Note exactly
   which cloud or wave disappears and whether the camera is moving. Keep the session log.
2. Find a repeatable bad interval with a short run and `DOL_AURORA_FRAME_INTERP_LOG_FRAMES=1`, then dump no
   more than 30 frames of it: `DOL_AURORA_FRAME_INTERP_DUMP` names a private output folder, and
   `DOL_AURORA_FRAME_INTERP_DUMP_FROM` and `DOL_AURORA_FRAME_INTERP_DUMP_TO` must both be set.
3. Compare the real and in-between frames. If only in-between frames fail, run that range once with
   `DOL_AURORA_FRAME_INTERP_TRACE=first-last` to see how the draw was matched or rejected.
4. Compare with 0.4.0 at the same view, which reporters say is much better. Remove the diagnostic variables
   afterwards, and don't use a traced run to claim speed.

## 5. Windows-only reports

Use `python3 scripts/triage_session_log.py session-*.log` on any attached log.

| Issue | What to do |
| --- | --- |
| #76 Forsaken Fortress soft lock | Try to reproduce on the tower with the Moblins; it may be the original game's behaviour. |
| #59, #86, #137, #159, #246 slowdowns | The logs say `game-thread` for nearly every slow second; the speed work is in [PERFORMANCE.md](PERFORMANCE.md). Measure the same scenes on each new build. |

Close an issue only when the reporter confirms the fix, or with a clear explanation.

### Reading the cutscene sound lines (#65, #97)

Every build writes one `[demo] end` line per cutscene, and `[audio-lost]` when a cutscene, or the intro
story's streamed music, goes silent for 6 seconds.

| What the log shows | What it means | Where to look |
| --- | --- | --- |
| `cues=0` on a cutscene that should have sound | The cutscene never asked for its sounds: its sound track didn't run. | Scene/cue dispatch and timing. Record the actual `[chassis]` settings: Windows 0.5.0 enables supported native accelerators by default; the Mac comparison can have them off. |
| `missing` above 0, with `[demo-sound] ... no sound` lines | The cutscene asked, but the game couldn't start the sound, usually because its sound data wasn't loaded in time. | Slow disc or ARAM reads; compare the slow seconds (`[fps-dip]`) around the cue. |
| `sounds` equal to `cues` but `[audio-lost]` or a long `silent=` | The sounds started but nothing reached the speakers. | The audio output: Smooth Motion (on by default in 0.4.0, off in `main`), and drops when the game falls behind real time. |
| `[music-stream]` reaches `state=4` then quickly returns to idle | The track started but stopped early; the reason is not established by this line. | Track lifetime, explicit stop requests, stream reads/decoder and scene changes. |
| No `[music-stream]` line with `state=4` during the intro | The streamed music never started playing. | Reading `Audiores/Stream/*.afc` from the disc image. |

On Mac, iPad and Windows `main`, the audio pacing defaults are the same (`BLUEWAKE_WALL_PACE=1`,
`DOL_AUDIO_NO_THROTTLE=1`, `BLUEWAKE_CLOCK=now`), but that does not establish identical execution.
The affected Windows 0.5.0 log has the native accelerators on, while the bounded Mac comparison has
them off. The experimental 60 Hz gameplay option is absent, and Smooth Motion is off by default.
Record actual settings and module capabilities before comparing platforms; none of these differences
is established as the cause of the stream ending early.
