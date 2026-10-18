# Vendored emibios

The free Game Boy Advance BIOS the Tingle Tuner's in-app GBA runs. It answers the GameCube's JoyBus multiboot
(the Tingle Tuner program upload) and provides the software interrupts the program calls, so the player supplies
no Nintendo BIOS dump.

| Item | Value |
| --- | --- |
| Project | emibios, <https://github.com/coolbho3k/emibios> (coolbho3k) |
| Version | release `0.1.6` (tag object commit `1dcd0005a72fc5b9232a2b05a821b5e1d7850bb3`), `gba_bios.bin` downloaded from the release |
| File | `gba_bios.bin`, 16,384 bytes, sha256 `ca56efafc53af12f6ba2eec722302dd2cb725f65dd2ed8afdeb5d27bc14e2d08` (equal to `bios.sha256` in the project's repo at that tag) |
| License | LGPL-3.0-or-later for the BIOS (`COPYING.LGPL-3.0`, the project's `LICENSE`). The BIOS is a data file the emulator loads, shipped unmodified, with its license text and the source link above. BlueWake is GPL-3.0-or-later. |
| Provenance | The README says it is not derived from, nor uses as reference, the retail BIOS ("observing and matching the behavior"), and discloses that LLMs wrote part of it. No independent audit; not legal advice. |

It is compiled into the app: `windows/CMakeLists.txt` turns `gba_bios.bin` into a C array at configure time
(`bluewake_emibios`). A player may point the setting at another 16 KiB BIOS file instead.
Replacing the file means updating the hash above.
