# How it works

Boat of Daphnes runs *The Legend of Zelda: The Wind Waker* (GameCube, USA, `GZLE01` revision 0) as a native
Windows program. It is a static recompilation: the game's PowerPC code is translated to C ahead of time, compiled
for x86-64, and run by a host program that stands in for the GameCube's hardware. No emulator interprets or
JIT-compiles the game's code while it plays.

The project builds on [BlueWake](https://github.com/chrissotraidis/bluewake) (GPL-3.0), which in turn builds on
RecompCore (a Dolphin-derived runtime) and DolRecomp (the translator). It adds Windows-specific work, game
options in the style of Better Wind Waker, a randomizer front end that uses LagoLunatic's
[wwrando](https://github.com/LagoLunatic/wwrando), and online co-op ("Anchor"). The menu and settings structure is
modelled on Ship of Harkinian's; no code from it is used.

This page describes the layers. Other pages go deeper:

- [game-code-map.md](game-code-map.md): the game functions and data the port intercepts or reads.
- [building.md](building.md): building from source.
- [anchor.md](anchor.md): online play.
- [randomizer.md](randomizer.md): the randomizer integration.
- [testing.md](testing.md): the test tools.

## The pieces

```
 your disc  ->  main.dol + 415 RELs  ->  DolRecomp  ->  C chunks  ->  gGZLE01_recomp.dll   (the "module")
                                                                          |
 boat-of-daphnes.exe  =  host (runtime/host/src)  +  GXRuntime / Aurora (runtime/recompcore)  +  app (windows/src)
                              |  loads the module, gives it a CPU state and memory, plays the hardware
```

| Layer | Where | Job |
| --- | --- | --- |
| Translator | DolRecomp, fetched at a pinned commit (`scripts/windows/pins.txt`) | Turns `main.dol` and the RELs into C source |
| Module | built by `scripts/windows/build.py` into `gGZLE01_recomp.dll` | The game's code, compiled; loaded with `LoadLibrary` |
| Host runtime | `runtime/host/src` | Devices, scheduling, the hooks that change the game |
| Renderer and device library | `runtime/recompcore/GXRuntime`, Aurora | GX command processing, DVD, EXI, audio DMA, WebGPU rendering |
| App | `windows/src`, `windows/compat` | Window, menus (Dear ImGui), settings, online play, randomizer screens |

## Static recompilation here

The game ships as one executable, `main.dol`, and about 415 dynamically loaded modules, `.rel` files, one for most
actors. DolRecomp reads each of them and writes C: a function per block of guest code ("chunk"), which reads and
writes a `CPUState` (registers, condition bits, floating-point state) and guest memory. A guest branch becomes a
return to a dispatcher or a direct call. The whole generated source is hundreds of files and a very large amount of C. The build
checks it against a digest pinned in `scripts/windows/pins.txt`, so a build that translated something different is
caught.

The module also holds a table of the game's modules, the REL load addresses and the option tables described below.
`build.py` compiles it with clang from Visual Studio. This is the long step of a build.

**Why the module is built from your disc.** The translated C and the DLL compiled from it are derived from
Nintendo's code. They cannot be published, so the repository holds the translator's inputs and the build scripts
only, never output. The builder extracts `main.dol` and the RELs from the disc image you give it, translates them,
and compiles them on your PC. Anything that came from the disc stays in your build folder. See
[building.md](building.md).

## The host: devices around the module

The host program (`runtime/host/src`, with the library in `runtime/recompcore/GXRuntime`) loads the module,
creates the guest's memory (main RAM and ARAM), and calls
into the module's dispatcher. It then plays the parts of a GameCube that the game talks to.

- **Cycles and deadlines** (`cycle_domain.c`, `interrupt_sources.c`, `scheduler_contract.c`). Generated code
  counts guest cycles (the guest clock is 486 MHz) and returns to the host when its budget runs out. The budget is
  the distance to the nearest device deadline: the next vertical retrace, a DSP or disc event, a serial-interface
  transfer. The host runs the devices, raises interrupts, and delivers them only where it is safe for the guest's
  scheduler. Keeping the guest's time independent of host speed is what makes runs repeatable.
- **VI, the video interface** (`vi_clock` in GXRuntime). The retrace drives the game's frame loop (the game runs
  at 30 frames a second, two retraces each). The renderer can present more frames than the game runs; the
  settings menu has an interpolation option for this.
- **SI, pads and the Game Boy Advance** (`host_input.c`, `ui_pad.c`, `gc_adapter.c`, `gba_link.c`). The game reads
  pad state through the serial interface. The host merges the keyboard, SDL game controllers, and a native driver
  for the Wii U GameCube controller adapter (WinUSB, reading the adapter directly). The Tingle Tuner's Game Boy
  Advance is an mGBA core running a free BIOS on its own thread, joined to a serial channel as a JoyBus device.
- **DSP and audio** (`dsp_adapter.cpp`, `audio_*.c`). Dolphin's DSP code runs the game's audio microcode, high
  level by default. The sound mixer's output goes to SDL audio. Volume sliders and per-category volumes write into
  the game's sound manager tables (see [game-code-map.md](game-code-map.md)).
- **DVD** (`gxruntime/dvd.c`, `disc_import.c`, `mods_host.c`). Disc reads come from the disc image. An overlay lets
  files from the `mods` folder, a custom player model, or a randomizer seed replace files at their disc paths
  without touching the image.
- **EXI, the memory card and IPL** (`card_runtime.c`, `ipl_sram.c`). Memory card slot A is a file on disk
  (`Save/GZLE01.card`); the console's settings (language, sound mode) are `Save/sram.bin`. The game's own save
  code runs unchanged against the card image.
- **GX, the graphics processor** (`runtime/recompcore/GXRuntime`). The game writes GX commands through the FIFO.
  GXRuntime parses them and issues draws through Aurora, a rendering library built on Dawn (WebGPU), using
  Direct3D 12 (Vulkan is the alternative backend). Shaders for the GameCube's TEV pipeline are generated at run time and cached.
  Aurora's widescreen policy gives the picture any window shape: the game's 4:3 frame is the middle of a render
  target the window's shape, and full-width draws show more of the scene at the sides.

## Hook sites: how the host changes the game

Most changes to the game are made *in the translation*, not by patching the disc. The tool for this is a text file
of **option sites**, `mods/betterww/options.txt`. Each line is one of:

```
option NAME INDEX on|off TITLE...     a switch; INDEX is its slot (1-255)
site   WHERE NAME insn|imm16 VALUE    an instruction (or its low 16 bits) the translator can emit two ways
hook   WHERE NAME before|after ID     a call from the translated code into the host
write  WHERE NAME f32|u32|u16|u8 VALUE a value written into guest memory at boot
```

`WHERE` is a `main.dol` address or `MODULE.rel+OFFSET`. `scripts/mods/game_options.py` turns the file into the
site list DolRecomp takes (`--option-sites`) and the option table compiled into the module.

- A **site** is translated both ways: the original instruction and the replacement. Which one runs depends on
  the switch for its option, a byte in a flag array shared with the host, so the player can turn an option on or
  off while the game runs.
- A **write** is applied when the option is on and undone when it is off. The host keeps what the memory held
  before.
- A **hook** is the way to put host code into the game. At that instruction the translated code calls
  `native_hook(cpu, id)` in `runtime/host/src/game_options.c`. The function can read and change registers and
  memory. It returns 0 to carry on at the next instruction, or the address of a guest function to run first: the
  host sets the link register to the hook site, so the guest function returns into the hook, which runs again
  with the result in `r3`. Several features use this to call game functions from native code (loading a model,
  starting a sound, creating an actor). Where a call may take several game steps, `guest_coro.c` runs the native
  code as a coroutine that can wait on those calls.

The hook ids in the file are the identifiers `native_hook` switches on. Ids 1 to 5 come from Better Wind Waker,
6 to 17 are the widescreen work, 18 to 26 are fixes taken from wwrando, 30 and up are the port's own (puppets,
PvP, switch sync, runtime fixes, file select, Bunny Hood). They are listed with their game functions in
[game-code-map.md](game-code-map.md).

Two cautions follow from how this works:

- A hook must be in the module. Adding or moving a hook site means translating and compiling the module again,
  which takes an hour or more, so sites are collected and added in batches.
- A hook is a call from translated code, so it runs on the game thread. Host code that other threads touch (the
  app, the network) has to take its own locks; the puppet, switch-sync and team-sync code do.

Larger sets of changes use **mod variants**. The module can contain alternative chunks for a mod (the randomizer's
patched code is one) and a layout of modules for it, selected when the game starts or, for the randomizer, while
the game runs.

## What stays out of the game's own data

The port keeps game memory consistent with what the game expects:

- Native code writes to the game's structures only at points where the game is not using them: after the
  per-frame draw loop, before the collision checks, or when no event is running.
- Features that need to draw something extra (other players, the Bunny Hood) build a second model with the
  game's own functions and enter it into the game's draw lists. They do not alter the player actor.
- Saves are the game's own memory-card format. Online sync reads and merges the save structure in memory; it does
  not use a separate format.

## The app layer

`windows/src` holds the Windows program around the host:

- `win_entry.c`: the entry point. It sets default paths beside the executable, starts the session log and crash
  handler, then calls the host's `main`. Everything is kept next to the executable, as in Ship of Harkinian:
  settings, the card, mods, logs, caches.
- `win_disc.c`, with `runtime/host/src/disc_import.c`: on first launch, the disc you pick is checked and
  `main.dol` and the RELs are extracted into the cache folder.
- `win_settings.cpp` and `ship_menu.cpp`: the settings menu, drawn with Dear ImGui over the game (open it with
  F1 or Esc). It has headers for Settings, Enhancements, Randomizer, Network and Dev Tools. Settings are
  named values stored in `boat-of-daphnes.json`; the menu applies them while the game runs, and the settings
  that the host reads once at startup become environment-style variables before it starts.
- `anchor.cpp`, `anchor_client.cpp`, `anchor_content.cpp`: online play ([anchor.md](anchor.md)).
- `rando_flow.cpp`, `rando_pack.cpp`, `rando_saves.cpp`, `randomizer_menu.cpp`, `check_tracker.cpp`: the
  randomizer ([randomizer.md](randomizer.md)).
- `mods.cpp`: the mods folder. Folders and `.zip`, `.rar` and `.7z` archives are entries in a priority list;
  their files go through the DVD overlay.
- `cosmetics_editor.cpp`, `link_colors.cpp`, `model_colors.cpp`, `hud_colors.cpp`: colour and model options.
- `item_tracker.cpp`, `check_tracker.cpp`, `notifications.cpp`, `input_viewer.cpp`, `input_editor.cpp`,
  `gba_window.cpp`: windows and overlays.
- `adapter_driver.cpp`: installs the WinUSB driver for the GameCube controller adapter.

The app talks to the game only through the host's C interfaces (`game_state.h`, `puppet.h`, `team_sync.h`,
`rando_seed.h`, and so on). `game_state.c` reads the current stage, room and Link's position on every retrace; the
app uses it for the room list, teleports and the session log.

## Where things are kept

Beside `boat-of-daphnes.exe`:

| Path | Contents |
| --- | --- |
| `gGZLE01_recomp.dll` | The module |
| `game/` | The disc image and the files extracted from it (built from your disc) |
| `boat-of-daphnes.json`, `imgui.ini` | Settings and window layout |
| `Save/` | `GZLE01.card`, `sram.bin`, `states/` |
| `mods/`, `mods/textures/` | Mods and texture packs |
| `Randomizer/` | Seed packs and spoiler logs |
| `logs/` | Session and crash logs |
| `cache/` | Shader caches and files made from your disc on first launch |

A rebuild replaces the program files only; the player folders are not touched.
