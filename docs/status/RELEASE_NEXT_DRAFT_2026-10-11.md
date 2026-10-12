# Next release: candidate record (draft)

Prepared on October 11, 2026 for Chris to decide. Nothing here is frozen: the version, the commit and when to build
are Chris's call ([GOAL_LOOP.md](../GOAL_LOOP.md), S4). Fill in each check with the device, the commit and what was
seen; a row stays "not yet" until someone runs it. Previous release: [0.7.0](RELEASE_0.7.0.md), October 10.

## What `main` has since 0.7.0

| Change | Issue | Ships to | Checked so far | Still to check |
| --- | --- | --- | --- | --- |
| **Linux download:** the first ready-made Linux build, an AppImage (cforain's setup, #217, #226; the shared build container, #254, merged October 12, with `--clang22` for the release build); the module about 50 MB smaller and quicker to load with RELR on glibc 2.36 or newer (pdale-boop, #268) | #56 | Linux | Source builds by five testers; the AppImage CI; RELR: all 2.2 million pointers the same and identical checkpoints on Arch | A contributor's own-disc AppImage, the release check, a Steam Deck run |
| **Faster:** the host's checks around each block of game code do less (pdale-boop, #242) | #59, #246 | Every platform | Linux: 5 to 8% on slow CPUs, identical checkpoints | The release build |
| **Faster:** returns into another chunk skip the dispatch's search, and direct calls skip the chassis for GXBegin (Elliott Tate, #234, #235) | #59 | Windows, Linux, Android builders | 811 of 811 dispatches on the prepared source; tests | The release build |
| **Faster on Linux:** draw fusion on, as on Windows (#247) | #215 | Linux | jkoehler11 on a Steam Deck: a tenth of the draws, nothing missing | Forest Haven on Linux |
| **Faster to build** on Mac, iPhone and iPad: code training never ran compiles at `-O1` (#252) | #104 | Mac, iPhone, iPad | M3 Max: 61 min of compile instead of 79; iPad Pro: the same speed in two places | PadMint's `player-check.sh` |
| Rebuilding after an app change takes about a minute (pdale-boop, #243) | none | Windows, Linux builders | Windows and Linux; Mac mods step byte-identical | none |
| Swap L and R (the triggers) under Controls (#248) | #244 | Windows, Mac, Linux | CI test | A controller |
| The developers' stage select, off by default (pdale-boop, #210, #250); once it's used, saving is off until a restart, F7 asks first, and picked cutscenes get their story flag (#266) | #251 | Windows, Mac, Linux | Played on Windows and Linux; no cost when off; #266 played on Windows, Mac build and card test | The 468-place sweep on the candidate; #266 on a Mac or Linux by hand |
| The Mac builder takes `--lean-blocks` and `--lean-memory`, the Windows builder's preparations, off by default (Elliott Tate's `lean_memory.py`, #257) | #59 | Mac builder | M3 Max: lean memory 5.5% faster at Outset and 6% in Dragon Roost Cavern, same blocks ([record](SPEED_PROFILE_2026-10-11.md)) | On by default in the Windows and Linux builders since October 12 (Chris's call); x86, Deck and phone numbers still wanted |
| `--lean-deadlines` in the Mac, Windows and Linux builders, off by default (#264), and Mac training at `-O1` with lean memory so the trained module links (#270) | #59, #215 | Every builder | M3 Max: 3.7 to 4.9% on top of lean memory untrained; trained, lean memory and lean deadlines 6.3 to 7.4% faster than lean blocks alone; same game state | On by default in the Windows and Linux builders since October 12 (Chris's call); a Windows PC, a Deck and a phone |
| The lazy overlap watch, on by default since October 12 (`BLUEWAKE_LAZY_OVERLAP=0` turns it off): the host stops reading the scene's overlap phase at every direct call, and the game module answers the per-call question itself (#274) | #59, #215, #93 | Every platform | M3 Max: 3.9 to 4.7% faster on top of lean memory and lean deadlines; identical checkpoints, also through four scene changes; every one of 708 M inline answers agreed with the host | A Windows PC, a Deck and the Fold 7 |
| **The Wind Waker HD look, first piece:** islands on the horizon from further away, one switch in every desktop menu and on iOS, off by default ([WWHD_LOOK.md](../WWHD_LOOK.md)) | #57 | Every platform but Android | Mac, from a save state: 15 island models instead of 7, an extra island in a captured frame | Windows and Linux by hand; an iOS build |
| CI actions brought up to date (Mrlinkwii, #273) | none | CI | Every check passes with them | none |
| **Fixed:** the sea chart shows each island's own map, and the Pictobox preview the new photo: the cache-flush fallback is on by default (`BLUEWAKE_CACHE_FLUSH_FALLBACK=0` turns it off) | #263, #13 | Every platform | The #263 reporter on Windows (opt-in): every map right, frame rate and Pictobox unchanged; Mac: no measurable cost, identical checkpoints | A physical iPad |

Not in it yet: Android (#93, waits on speed and Chris's bar).

## Checks before publishing

| Check | Who | Result |
| --- | --- | --- |
| Freeze: `version.json`, the candidate commit here | Chris decides; Codex does it | not yet |
| Windows build from the candidate on Chris's PC, with the play checks in [WINDOWS_TASKS.md](../WINDOWS_TASKS.md) sections 1 and 2 | Chris's PC | not yet |
| Linux AppImage from a contributor's own disc; `check_appimage_abi.py` at 2.39; the release check | cforain or jkoehler11 | not yet |
| The app-only IPA, PadMint's `audit`, and PadMint's `scripts/player-check.sh` before tagging and after publishing | Codex, on this Mac | not yet |
| The 468-place warp sweep with the stage select (`scripts/stage_select_sweep.py`) | pdale-boop or Codex | On `main` `d73ac95` (not the candidate), a Mac, an early file: 446 of 468 arrive, the card unchanged after all 468; the rest are the noted test rooms and crash rooms and late-game cutscenes the file can't reach ([STAGE_SELECT.md](../STAGE_SELECT.md)). Still to do on the candidate, with a late-game file |
| The benchmark at Outset and Dragon Roost Cavern against 0.7.0 | Codex | not yet |
