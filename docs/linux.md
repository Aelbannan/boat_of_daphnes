# Boat of Daphnes on Linux

A native x86-64 Linux build of the same app as the Windows one: the settings menu, Anchor online play, the randomizer
and its trackers, the in-app Game Boy Advance, mods and the rest. The game itself is not included: you build it from your own
disc, on your own PC, and the result stays yours (it holds code translated from the game).

What you need:

- a PC with an x86-64 CPU that has AVX2 (Intel Haswell, 2013, or AMD Ryzen, or newer) and a Vulkan or OpenGL
  driver (Mesa for AMD and Intel, the NVIDIA driver);
- your own USA GameCube disc image of The Wind Waker, revision 0 (GZLE01): an `.iso` or `.gcm`, or a Dolphin
  `.rvz`/`.wia`/`.gcz`/`.ciso` (converting those needs Rust's `cargo` once, or convert them to ISO in Dolphin first);
- several GB of free disk space and, for the build, 8 GB of memory or more (the long step uses a few GB per core; the
  builder lowers its parallel jobs by itself when memory is short).

## Playing: build it from your disc

```sh
git clone https://github.com/AgitationSkeleton/boat_of_daphnes.git
cd boat_of_daphnes
python3 scripts/linux/build.py /path/to/TheWindWaker.iso               # add --rando-variant for the randomizer
./build/linux/boat-of-daphnes/boat-of-daphnes
```

`--rando-variant` builds the randomizer into the game module as one more mod (the module compile grows by about a
third); the Randomizer menu and the seed generator are in every build either way.

The build checks the disc, extracts it, translates the game's PowerPC code to C with DolRecomp (built here), prepares the
same optimized, modded source the Windows build uses, compiles it into `gGZLE01_recomp.so` with clang (the long step:
30 minutes to a few hours, by core count), builds the app, and packs everything into `build/linux/boat-of-daphnes/` laid out
like the Windows folder. Running it again continues where it stopped; unchanged steps are skipped. There is no
profile-guided optimization on Linux yet, so the game runs a few percent slower than an equally prepared Windows build.

Packages to install first (Ubuntu 24.04 / Debian names; other distributions have the same things under their names):

```sh
sudo apt install git cmake ninja-build clang lld python3 python3-venv pkg-config \
    g++-13 libstdc++-13-dev libssl-dev libudev-dev zlib1g-dev \
    libwayland-dev libxkbcommon-dev libx11-dev libx11-xcb-dev libxrandr-dev libxext-dev libxi-dev libxcursor-dev \
    libxss-dev libxtst-dev libgl1-mesa-dev libvulkan-dev libasound2-dev libpulse-dev libdbus-1-dev
```

- clang 17 or newer. clang 18 and older cannot read libstdc++ 14's headers, so the builder points them at gcc 13's
  (`g++-13`) when it is installed; with clang 19 or newer any gcc is fine.
- CMake 3.25 or newer, Ninja, Python 3.10 or newer for the builder.
- **python3.12** with its `venv` module for the randomizer's generator (wwrando breaks on Python 3.13 and newer). Ubuntu
  24.04's `python3` is 3.12. On a distribution with a newer Python, install 3.12 beside it (pyenv, uv, the distribution's
  `python3.12` package): the builder looks for `python3.12` first.
- At run time: a Vulkan or OpenGL driver, and for the disc chooser either an XDG desktop portal or `zenity` (or pass
  `--disc FILE`).
- Optional: Rust's `cargo` (compressed disc images), `appimagetool` and `desktop-file-utils` (the AppImage).

Useful options (the same names as `scripts/windows/build.py`, whose pipeline this runs): `--out DIR`, `--jobs N`,
`--no-mods`, `--rando-variant`, `--march x86-64-v2` (for a CPU without AVX2; the build lowers it by itself when your CPU
lacks the default x86-64-v3), `--install-module`, `--appimage`.

### The AppImage

`python3 scripts/linux/build.py DISC --install-module --appimage` makes `build/linux/boat-of-daphnes-x86_64.AppImage`:
the program, the DSP ROMs, the randomizer runtime and the libraries the program links (OpenSSL among them), for glibc 2.39
or newer (the audit, `scripts/linux/check_appimage_abi.py`, enforces it). It carries **no game module and no disc**: the
module you built is in `~/.local/share/boat-of-daphnes/gGZLE01_recomp.so` (`--install-module` put it there), and on first
launch the app asks for your disc and prepares what it needs from it into the same folder. `--with-module` puts your
module in the image: a personal image, never to be shared.

The randomizer's generator uses the system's `python3.12` (the runtime's venv points at it), so the AppImage wants that
installed too. The image does not bundle Vulkan or OpenGL drivers; it uses the system's.

## Where your files are

Like the Windows build, the program keeps everything beside itself when it can: `Save/` (the memory card, console
settings, save states), `mods/`, `Randomizer/`, `logs/`, `cache/`, `boat-of-daphnes.json` (the settings) and
`imgui.ini`. When its folder cannot be written to (an AppImage's mount, `/usr/bin`) they are in
`$XDG_DATA_HOME/boat-of-daphnes` instead, usually `~/.local/share/boat-of-daphnes`. `BLUEWAKE_DATA_DIR=DIR` chooses another
place. A rebuild never touches them. Back up `Save/GZLE01.card`.

Session logs are `logs/session-*.log` (the seven newest are kept); a crash adds `logs/crash-<pid>.log` with the faulting
address and a stack, and a freeze of ten seconds writes every thread's stack into the same file (`addr2line -e` on the
module or program names the frames).

## Controls, controllers and the GameCube adapter

Keyboard: arrows D-pad, J A, K B, U X, I Y, W/A/S/D control stick, H/F/T/G C-stick, E/R L/R, Q Z, Return START.
F1 or Esc opens the menu, F11 toggles fullscreen, F9 the frame rate. Game controllers work through SDL3 (Xbox,
PlayStation, Switch Pro and most others); the menu's Controls page binds them.

The official GameCube adapter (Wii U, `057e:0337`) is read by the program itself through hidraw, and needs a udev rule that
lets your user open it. Create `/etc/udev/rules.d/51-gcadapter.rules`:

```
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", TAG+="uaccess"
```

then `sudo udevadm control --reload-rules && sudo udevadm trigger` and replug the adapter. The menu's driver button does
nothing on Linux (there is no driver to install); Settings > Controls shows whether the adapter is Connected, In Use by
Another Program (close Dolphin) or Not Found. The adapter's rumble needs its second USB cable plugged in.

## Wayland and X11

SDL3 picks Wayland when the session is one and X11 otherwise. To force one: `SDL_VIDEODRIVER=x11` or
`SDL_VIDEODRIVER=wayland`. The window remembers its place and size on X11; Wayland compositors decide placement
themselves. If fullscreen misbehaves under a compositor, F11 again, or start with `--window WxH`. On a Steam Deck, add the program as a non-Steam game and run it in Desktop
mode first to choose the disc; Gaming mode controls work through Steam Input.

Aurora uses Vulkan by default and falls back to OpenGL. `--help` lists the options (`--aspect`, `--scale`, `--fps`,
`--safe-mode` ...).

## Not on Linux yet

- the GameCube adapter driver install (not needed), the Windows minidump on a freeze (the stack report stands in),
  profile-guided optimization of the module and app, a progress window while the disc is prepared (the steps and their
  percentages are in the session log), Alt+Enter without the game also seeing Return as START.

## For developers

The app is the same source list as Windows: `windows/CMakeLists.txt` builds both (`cmake -S windows -B build/linux
-G Ninja`). What differs lives in `windows/src/platform/` (`bod_platform.h`: the executable's folder, child processes
with pipes, opening files, the clipboard, HTTP, a WebSocket client; `platform_win32.cpp` is the Windows code that used to
be inside the app's files, `platform_posix.cpp` and `net_posix.cpp` are Linux's, with the system's OpenSSL for TLS and
certificate checks), `linux_entry.c` (the entry shim), `linux_disc.c` (the disc chooser and preparation),
`linux_crash.cpp` (the crash reporter and hang watchdog) and `platform/adapter_driver_linux.cpp`.
`runtime/host/src/guest_coro.c` has a ucontext implementation of its coroutines.

`scripts/linux/build.py` subclasses `scripts/windows/build.py`'s `Builder`, so a change to a preparation pass reaches both.

CI is `.github/workflows/linux.yml` (and `public/.github/workflows/linux-app.yml` for the public repository): on
ubuntu-24.04 it builds the app with clang 18, the translator (DolRecomp) and the randomizer runtime, runs the source-only
host tests with ctest, packages an AppImage around an **empty** stand-in module (`build/ci-fixture/.../gGZLE01_recomp.so`, a
few kilobytes, the only file with that name anywhere in the build) and runs the ABI audit and `--help` on it. Nothing from a
disc is ever built or stored there. For a quick syntax check of the Linux code on another system, an extracted Ubuntu
sysroot and `clang --target=x86_64-linux-gnu -fsyntax-only` is enough to find missing declarations.
