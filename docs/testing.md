# Testing

The tests need either nothing (source-level checks) or your own disc (anything that runs the game). None of them
are part of a player's build. Test outputs, states and recordings hold game data, so they are written under
`build/` or `BOD_SCRATCH` (see [building.md](building.md)) and are never committed.

## What there is

| Tool | What it checks | Needs |
| --- | --- | --- |
| `scripts/windows/scenario.py` | Starts the game, scripts a situation, presses buttons, takes pictures, checks the log. The base of the others. | A build and your disc |
| `scripts/windows/bench.py` | Speed of the game across a fixed tour, and whether two builds play identically. | A build and your disc |
| `scripts/windows/stability.py` | Whether the game holds its frame rate in busy places, in real time. | A build and your disc, an otherwise idle PC |
| `scripts/windows/verify_release.py` | A packaged build, the way a player meets it: first launch, new game, save, relaunch, load. | A zip package and your disc |
| `scripts/windows/ui_golden.py` | The wide-screen HUD and pause screens against saved reference pictures. | A build and your disc |
| `tests/*.c`, `tests/*.cpp` | Small programs for the host and the renderer, built by CMake. | The compiler (no disc) |
| `tests/*.py` | Checks of the build scripts and generators. Some need the disc (`BOD_DISC`) or a wwrando checkout (`BOD_WWRANDO`) and skip or fail clearly without them. | Python |

Run one game at a time. The runner takes a machine-wide lock so that two scenarios, or a scenario and a compile,
do not overlap. Frame rates are only meaningful when nothing else is using the PC.

## The scenario runner

```
python scripts/windows/scenario.py --list
python scripts/windows/scenario.py outset
python scripts/windows/scenario.py ui_wide --size 2560x1080 --size 1440x1080
python scripts/windows/scenario.py anchor_puppets --exe build-appcheck/app/boat-of-daphnes.exe
```

By default it uses the package in `build/windows/boat-of-daphnes/` and the disc named by `BOD_DISC` (or `--iso`).
`--exe` and `--module` choose another app or module, such as an app built by itself against an existing module.
Other options: `--size WxH` (repeatable), `--cvar NAME=VALUE` and `--env NAME=VALUE` (settings and environment
variables added to every run), `--tag` (a suffix for the output folder), `--remake` (make starting states again),
`--timeout`.

Each run gets its own data folder, so a test never touches a player's settings, card or saves. Output goes to
`build/windows/scenarios/<name>/<size>/`:

- `run.log`: the game's log;
- one PNG per picture taken, and `sheet.png` with all of them;
- `result.json`: exit code, pictures, the values read, which expectations matched, and whether the run passed.

The exit code is non-zero when a run fails: an expectation did not match, the game timed out, or it exited badly.
The window opens at a fixed position far to the right of the main monitor without taking focus
(`BLUEWAKE_TEST_WINDOW_POS` changes it), and sound goes to a dummy driver unless the scenario asks for an audio
capture.

### How a run is timed

Time in a scenario is counted in **retraces**: the game's video frames at 60 per second. A cursor starts at 0.
`wait` moves it, and so do `press`, `hold`, `stick` and `cstick`, by their length. A step happens when the
cursor reaches it. For a scenario that starts from a saved state, 0 is the retrace the state was saved at. For
`start boot`, 0 is retrace 8600: the scripted opening has played to player control on Outset (the new-game story
is skipped).

### The file grammar

A scenario is a text file in `scripts/windows/scenarios/`, one command per line. `#` starts a comment.

| Command | Meaning |
| --- | --- |
| `description TEXT` | One line shown by `--list`. |
| `start boot` | Begin with the scripted opening, to player control on Outset. |
| `start boot raw` | Begin at power-on with no scripted presses; the scenario drives everything. |
| `start state NAME` | Begin from a saved state in `build/windows/states/`. |
| `size WxH` | Window size. |
| `card FILE` | A memory card from the states folder, copied into the run's `Save/`. |
| `cvar NAME VALUE` | A setting for this run, written to the run's own settings file. `rgb:RRGGBB` makes a colour. A non-number is text. |
| `env NAME VALUE` | An environment variable for this run. `{root}` in the value is the checkout's folder. |
| `audio` | Record the game's audio output to `audio.wav` in the run folder. |
| `wait N` | Move the cursor `N` retraces. |
| `press BUTTONS [N]`, `hold BUTTONS N` | Hold buttons for `N` retraces (default 2). Buttons are `A B X Y Z L R start up down left right`, joined with `+`. |
| `stick X Y N [BUTTONS]` | The control stick at X, Y (-128 to 127), for `N` retraces, optionally with buttons held. |
| `cstick X Y N` | The C-stick. |
| `give ITEM...` | Put items in their inventory slots (`telescope sail wind_waker picto_box bow` and so on; the names are in `scenario.py`). |
| `equip X|Y|Z ITEM` | Put an item on a button. |
| `health QUARTERS`, `set FIELD VALUE` | Set life (4 is one heart) or a named field (`life`, `max_life`, `rupees`, `magic`, `play_life`). |
| `warp STAGE:ROOM:POINT[:LAYER]` | A scene change as a door makes one, for example `sea:1:100` (on the boat). |
| `teleport STAGE ROOM X Y Z [ANGLE]` | Put Link at a place in a stage; the area reloads. |
| `poke ADDRESS=VALUE[/SIZE]` | Write guest memory. `*POINTER+OFFSET=VALUE` writes through a pointer read at that moment; `\|=` sets bits and keeps the rest. |
| `peek FIELD|ADDRESS[/SIZE]` | Log a value as `[test] peek ...`. A 4-byte read through a pointer is also logged as a float. |
| `shot` | Take a picture of the frame. |
| `resize WxH` | Change the window size mid-run. |
| `save_state NAME` | Save the game's state here, for other scenarios to start from. |
| `expect REGEX`, `expect_not REGEX` | The run's log must, or must not, match. |
| `end [N]`, `quit [N]` | Stop at retrace `N`; the default is the cursor plus 10. |

Example, `soft_reset_boot.txt` in short:

```
description Boot, load the file, play, hold B+X+Start; back at the title, no console reset.
start boot
size 1280x960
wait 300
press B+X+start 60
wait 600
shot
end
```

### States

A scenario that contains `save_state NAME` makes a state; others use it with `start state NAME`. States live in
`build/windows/states/`, which is git-ignored because a state holds game memory. A state remembers which module made
it, and the runner makes it again when the module has changed. Scenarios that start from a named state which no
shipped scenario makes (a played card, for example) need you to supply it.

### What the runner uses in the game

The runner talks to the game through environment variables read by the host. The scenario's commands become these:

| Variable | Used for |
| --- | --- |
| `BLUEWAKE_PAD_SCRIPT` | Buttons and sticks by retrace. |
| `BLUEWAKE_TEST_POKE`, `BLUEWAKE_TEST_PEEK` | Memory writes and reads by retrace (`runtime/host/src/test_control.c`). |
| `BLUEWAKE_TEST_WARP`, `BLUEWAKE_TEST_TELEPORT` | Scene changes by retrace. |
| `BLUEWAKE_TEST_RESIZE` | Window size changes. |
| `BLUEWAKE_SAVE_STATE`, `BLUEWAKE_LOAD_STATE` | State files. |
| `BLUEWAKE_CAPTURE_RETRACES` | Which frames to save as pictures. |
| `BLUEWAKE_MAX_RETRACES` | When to stop. |
| `BLUEWAKE_DATA_DIR` | The run's own data folder. |

Other `BLUEWAKE_*` variables switch individual features for tests (the online-play scenarios use a few to feed a
local server and to place a scripted second player). Their meaning is in the source file that reads them.

### Shipped scenarios

`scripts/windows/scenarios/` in this repository holds a selection that depends on nothing machine-specific:

| Scenario | Purpose |
| --- | --- |
| `outset` | Makes the `outset` state (the scripted opening to player control). |
| `ui_wide` | The HUD and two pause screens at 21:9, used by `ui_golden.py`. |
| `bench_tour`, `stability_tour` | The tours used by `bench.py` and `stability.py`. |
| `soft_reset_boot` | The three-button soft reset from a normal boot: back at the title, no console reset. |
| `anchor_puppets`, `anchor_sea_chart`, `anchor_pvp_hook`, `anchor_pvp_hook_on` | Online play against a local server: other players as puppets, their arrows on the Sea Chart, and the PvP hooks. |
| `gba_windfall_linked_quiet`, `gba_windfall_off` | Windfall with the Tingle Tuner's Game Boy Advance linked and not linked. |

The `anchor_*` scenarios say at their top how to start the local server and the scripted players
(`server/anchor/test`); see [anchor.md](anchor.md).

## `bench.py`: speed and sameness

```
python scripts/windows/bench.py run --label base [--exe EXE] [--module DLL] [--mode headless|rendered|both]
python scripts/windows/bench.py compare base-headless perf1-headless
```

`run` plays `bench_tour.txt` through a series of places (Outset, Windfall and its crowd, Dragon Roost Cavern,
Forest Haven, and the sea) as fast as the PC allows, with frame interpolation off so only the game is measured.
The first retraces of each stop (loading, shader compiles) are left out. For each stop it prints:

- **game fps**: retraces a second divided by two (unpaced: what the PC can do, not capped at 30);
- **Mcyc/frame**: million reference cycles the game thread ran per game frame, a measure of its own CPU time that
  leaves out time other programs take;
- **worst ms**: the longest gap between retraces.

`headless` runs the game alone; `rendered` adds the graphics worker and the GPU. Two runs of one build differ by a
few percent, so run each build three times, alternating, and give `compare` comma-separated labels (it uses
medians).

The run also saves *guest checkpoints*: hashes of the CPU and memory every 300 retraces. `compare` says whether two
runs match and where they first differ. This is how a change that should not alter the game's behaviour (a faster
translation, a different compile flag) is checked: the checkpoints must be identical.

## `stability.py`: does it hold its frame rate

```
python scripts/windows/stability.py run --label NAME [--exe EXE] [--module DLL] [--size 3440x1381] [--interp 60] [--gba]
python scripts/windows/stability.py compare A B
```

Real time, at settings a demanding player would use (interpolated output at 60, vertical sync on, a wide window):
Link runs in circles at points on Outset, in Windfall's crowd and at Dragon Roost while the camera pans, for about
twelve seconds each. The first two seconds of a point are left out. For each point it reports the lowest, median and
worst second of game frames, the longest frame, and the number of seconds under 29.5 game frames per second.
**Pass** is no dips at any point. `--gba` links the Tingle Tuner first so its serial stream runs through the tour.
It needs an idle PC; other programs' load shows up as dips.

`scripts/windows/fps_check.py` reads a scenario's `[fps]` log lines and reports the minimum, 5th percentile and
mean against a target; `scripts/windows/perf_lab.py` runs timed real-time sessions of a prepared game folder.

## `verify_release.py`: the package, as a player meets it

```
python scripts/windows/verify_release.py PATH\TO\boat-of-daphnes-VERSION-windows-x64.zip [--name rc1] [--only fresh_v1,fs_inplace]
```

Everything happens under `BOD_SCRATCH`, in a fresh folder; the checkout's build folder is not used. It:

1. unzips the package into a new folder with no saves;
2. does the first launch with your disc picked, and checks that the game files, Swift Sail and Link-colour assets and
   the randomizer's modules were made;
3. runs the player paths through the real menus as pad-script scenarios using that copy's own executable and module:
   vanilla (new game, play, save, quit; relaunch, load, play), randomizer (generate from the menu, switch in place,
   new game, play, save; relaunch, load, play), file-select switching and reset paths, and a ten-minute roam in
   Windfall and Outset from a played card;
4. writes a summary (`summary.md`, `summary.json`, `index.html` with a contact sheet per path) and exits non-zero if
   any path failed.

A pass is: the scenario's expectations met, no `unmapped pc` and no `PPCHalt` in the log, and a clean exit. Frame
rates are recorded but not judged. Each scenario is its own `scenario.py` run, so the machine lock is taken and
released per scenario.

## `ui_golden.py`: pictures of the interface

```
python scripts/windows/ui_golden.py --exe EXE --module DLL --make   # from a build known to be right
python scripts/windows/ui_golden.py --exe EXE --module DLL          # check a build against them
```

Runs `ui_wide` (the HUD and two pause screens at 21:9) and compares each picture with the reference taken at the
same retrace; the run is deterministic. A picture passes when fewer than 0.5 percent of its pixels differ by more
than 24 levels. It exists because a pause backdrop that stopped at 4:3 was once not caught by anything else. After a
deliberate interface change, make the references again. They stay on your machine; they are pictures of the game and
are not committed.

## Unit tests

C and C++ tests of host pieces (cycle accounting, interrupt delivery, the memory card, the disc unpacker, the GX
renderer's shaders, the GameCube adapter's protocol, the mod archive reader and more) are in `tests/` and are built
by CMake when `-DBLUEWAKE_WINDOWS_REGRESSION_TESTS=ON`, or one at a time with
`python scripts/windows/build_app_only.py --target <name>`, which leaves the program in `build/windows/app/`. Python
tests in `tests/` check the translator's inputs and outputs, the generators and the randomizer's patch handling;
run a test file directly with Python (`python tests/test_composite.py`); they are plain scripts, with unittest in
some. Tests that need the disc or a wwrando checkout read
`BOD_DISC` and `BOD_WWRANDO`.
