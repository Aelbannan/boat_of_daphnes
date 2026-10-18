# Vendored libwdi

The WinUSB driver installer behind Settings > Controls > GameCube Adapter > Install Adapter Driver.

| Item | Value |
| --- | --- |
| Project | libwdi (Windows Driver Installer library), <https://github.com/pbatard/libwdi>, Pete Batard and contributors; the library behind Zadig |
| Version | commit `30df0c0e051b0132c4b9ebed8c054bc8eb3aaaec` of `master` (imported 2026-10-05) |
| License | LGPL-3.0-or-later (`COPYING-LGPL`; `COPYING` is the GPL text the repository carries). BlueWake is GPL-3.0-or-later, which can use an LGPL-3.0 library; the library is built as a separate static archive from unmodified sources and its notices stay. |

## What is here

- `libwdi/`: the library's `*.c`, `*.h`, the INF and catalog templates (`*.in`), `libwdi.rc` and `libwdi.def`,
  byte-identical to the repository. Only `logging.c tokenizer.c vid_data.c pki.c libwdi_dlg.c libwdi.c` go into the
  library, with `installer.c` (the helper libwdi starts with a UAC prompt) and `embedder.c` (which embeds the helper
  and the INF templates as `embedded.h`) built by `CMakeLists.txt`.
- `config/config.h` and `config/build64.h`: ours. WinUSB only, x64 only, no co-installers or other drivers embedded
  (Windows 10 and 11 carry `winusb.sys`).
- Not taken: the MSVC solution, autotools files, examples, and Zadig.

## What it does when used

`wdi_prepare_driver` writes a WinUSB INF for the adapter's USB id, makes a catalog and signs it with a self-signed
certificate (the signing is libwdi's `pki.c`); `wdi_install_driver` starts the embedded helper with a UAC prompt, which
puts that certificate in the machine's Trusted Publishers store and installs the driver. The menu says so before the
install starts. BlueWake never runs it by itself: only the button does.
