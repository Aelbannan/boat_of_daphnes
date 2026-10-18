# Building from source (Windows)

Boat of Daphnes is built on your PC from this repository and **your own disc** of *The Legend of Zelda: The Wind
Waker* (GameCube, USA, `GZLE01`, revision 0). The repository contains no game code, no game assets and nothing
translated from a disc. The build makes those on your machine, in a folder that is not tracked by git.

Only Windows 10 or 11 on x86-64 is supported by these scripts.

## What you need

| Requirement | Notes |
| --- | --- |
| Windows 10 or 11, x86-64 | The builder refuses other systems. |
| A CPU with AVX2 and FMA | The game module is compiled for `-march=x86-64-v3` by default (Intel Haswell, AMD Ryzen or newer). The builder checks your CPU and falls back to a lower level if it lacks it; `--march` picks one. |
| Visual Studio 2022 or newer | Install the workload **Desktop development with C++**, and add **C++ Clang Compiler for Windows** (and **MSBuild support for LLVM**). The builder finds Visual Studio with `vswhere`, runs its developer environment, and uses its clang. clang 17 or newer is required. |
| Python 3.10 or newer | Runs the build scripts. |
| Git | Used to fetch the translator at a pinned commit. |
| CMake 3.25 or newer, Ninja | `pip install cmake ninja` works. |
| `llvm-profdata.exe` | Comes with Visual Studio's clang. Needed for the optimisation training step; skip that step with `--no-train`. |
| Rust and cargo | Only if your disc is a Dolphin `.rvz`, `.wia`, `.gcz`, `.ciso`, `.nfs`, `.wbfs` or `.tgc` image. The builder builds the converter (`nodtool`) from crates.io on first use. An `.iso` or `.gcm` needs none. |
| Your disc image | `GZLE01` revision 0. The builder checks the game id and the revision and stops on anything else. |
| An internet connection | For DolRecomp (from GitHub, at the commit pinned in `scripts/windows/pins.txt`) and for Aurora's prebuilt Dawn and SDL3 packages, fetched by CMake. |
| Disk, memory and time | A first build takes an hour or more and tens of gigabytes. Each translated chunk takes 1 to 3 GB in clang, so the builder runs as many compile jobs as free memory allows (`--jobs` overrides). |

The vendored libraries (RecompCore's GXRuntime and Aurora, libarchive, mGBA, zstd, bzip2, libwdi) are in
`runtime/` and need nothing installed.

## Where the tools find things: `BOD_*` settings

Developer scripts read a few paths that are not in the repository. They are looked up in this order: the
environment variable of that name, then `local_paths.json` at the repository root, then the script's own fallback.
`scripts/bod_paths.py` does the lookup.

Copy `local_paths.example.json` to `local_paths.json` and fill it in. The file is not tracked.

| Name | What | Used by |
| --- | --- | --- |
| `BOD_DISC` | Your Wind Waker disc image | the test runner, randomizer tools, tests |
| `BOD_TWW_DECOMP` | A checkout of [zeldaret/tww](https://github.com/zeldaret/tww) | tools that need symbol names or cutscene lists |
| `BOD_WWRANDO` | A checkout of [LagoLunatic/wwrando](https://github.com/LagoLunatic/wwrando) | the randomizer runtime, Link colour masks |
| `BOD_SCRATCH` | A folder for test runs, benchmarks and release checks (default `build/scratch`) | the test tools |
| `BOD_MM_REF` | Optional reference meshes for the Bunny Hood model generator's development tools | development only |

`build.py` itself takes the disc as an argument; it does not need `BOD_DISC`.

## The full build

```
python scripts/windows/build.py "path\to\your-disc.iso"
```

Useful options (all are in `python scripts/windows/build.py --help`):

| Option | Effect |
| --- | --- |
| `--out DIR` | Output folder. The default is `build/windows`. Inside the checkout it must be git-ignored. |
| `--check-only` | Check the tools, the dependencies and the disc, then stop. |
| `--source-only` | Go as far as the generated source (extract, translate, compare with the pinned digest) and stop. This takes minutes and is a good first run. |
| `--variants-only` | Stop after the mods step, before the compile. |
| `--jobs N` | Parallel compile jobs. |
| `--march LEVEL` | CPU level for the module and app. |
| `--no-mods` | Leave out the widescreen and Better Wind Waker option sites. |
| `--no-train` | Skip the optimisation training (an untrained build is slower to run, faster to build). |
| `--conservative` | Build the plain translation, without the extra optimisations a default build prepares. |
| `--rando-variant` | Also build the randomizer's code into the module as a variant. Run `python scripts/windows/rando_runtime.py` first (see [randomizer.md](randomizer.md)). |
| `--console` | Build the program as a console application, so logs appear in the terminal. |

The builder runs in ten steps, each logged under `OUT/logs`, and reuses earlier results when their inputs have not
changed, so an interrupted build can be started again with the same command:

1. **tools**: Visual Studio's clang, the Windows SDK, CMake, Ninja, Git.
2. **dependencies**: DolRecomp at its pinned commit, checked out into `ref/dolrecomp`.
3. **disc**: verifies the image; converts a compressed image to an ISO.
4. **extract**: `main.dol` and the 415 RELs are read from the disc.
5. **translate**: DolRecomp turns them into C.
6. **generate**: assembles the composite source and compares it with the pinned digest. A mismatch stops the
   build (`--accept-new-composite` accepts a different digest on purpose).
7. **mods**: the widescreen and Better Wind Waker option sites, and the randomizer if asked.
8. **compile**: the game module, `gGZLE01_recomp.dll`. This is the long step.
9. **app**: `boat-of-daphnes.exe`.
10. **package**: assembles the output folder.

## The app-only build

When you change the host or the app (`runtime/host/src`, `windows/src`) and not the game module or any hook site, you
do not need to translate and compile the game again:

```
python scripts/windows/build_app_only.py [--jobs 2] [--reconfigure]
```

This sets up the same Visual Studio environment as the full build, configures `windows/CMakeLists.txt` with Ninja
and clang if needed, and builds the `BlueWake` target. It needs no disc. The executable lands in
`build/windows/app/boat-of-daphnes.exe`; the package folder is not touched, so copy the executable over the one in
`build/windows/boat-of-daphnes/` or run it from there with the module of an earlier full build.

`--target NAME` (repeatable) builds another target instead, for example one of the small test programs
(`bluewake_gc_adapter_proto_test`); `--rando-variant` compiles the randomizer's host tables, which a full
randomizer build made.

Do not run `cmake` or `ninja` straight from a plain shell: the scripts set up the compiler environment, and they
take a lock so that two builds on one machine do not run at the same time.

**When you need the full build again.** Whenever a hook or option site is added, moved or removed in
`mods/betterww/options.txt`, whenever the pinned DolRecomp or the generator scripts change, and whenever the
module ABI changes. The app refuses a module that does not match it and says to rebuild.

## What the output folder contains

`build/windows/boat-of-daphnes/` is the finished program. It is for you only.

| Path | Contents |
| --- | --- |
| `boat-of-daphnes.exe`, `*.dll` | The app, SDL3, Dawn and shader compiler DLLs, the Visual C++ runtime (copied so the folder runs on a PC without it) |
| `gGZLE01_recomp.dll` | The game module: code translated from your disc |
| `game/main.dol`, `game/rels/` | The game's executable and modules extracted from your disc |
| `game/GZLE01.iso` | A link to, or a copy of, your disc image |
| `dsp/` | DSP ROM files, used by the low-level audio mode |
| `assets/` | Files made from your disc: Swift Sail textures, Link colour masks, the Bunny Hood model |
| `rando/` | The randomizer's generator and its embedded Python, if it was built |
| `initial_pipeline_cache.db` | Aurora's starting shader pipeline cache |
| `BuilderProvenance.json`, `README.txt` | A build record (module hash, options, source commit) and a short note |

Besides it, `build/windows/` holds the intermediate results: `game/`, `translated/`, `composite-src/`, `app/`,
`logs/`, and test outputs.

When you run the program, it keeps its own data beside the executable: `Save/`, `mods/`, `Randomizer/`, `logs/`,
`cache/`, `boat-of-daphnes.json`. A rebuild never touches them. See [how-it-works.md](how-it-works.md).

## Nothing from your disc is ever committed

- `build*/`, `ref/`, `game/`, `local_paths.json` and files with disc, card or game-file extensions (`.iso`, `.rvz`,
  `.dol`, `.rel`, `.card`, and others) are ignored by git.
- `build.py` refuses an output folder inside the checkout that git does not ignore.
- The translated source, the module, the extracted `main.dol` and RELs, the disc image, saves and test states are
  all in ignored folders. Test states (`build/windows/states`) hold game memory and stay out of version control.
- The scripts that make assets from the disc (Swift Sail, Link colours, the Bunny Hood, the randomizer's patched
  RELs) write them into the output folder or the cache, never into the source tree. Test scenarios are text
  files of button presses and expected log lines.
- The module, the extracted files and the disc copy are yours. Do not share them or upload them, and do not attach
  them to bug reports.

If you contribute, check `git status` before you commit.

## When something fails

- Read the log named in the error under `build/windows/logs/`.
- "Visual Studio ... not found": install the workload and the clang component listed above, or run the script from
  an "x64 Native Tools Command Prompt".
- A compile that ends with "LLVM ERROR: out of memory": lower `--jobs`; the builder also retries with fewer jobs.
- A composite digest mismatch: the translator or the generator scripts differ from the pins. Check
  `ref/dolrecomp` is at the commit in `scripts/windows/pins.txt` and has no local changes.
