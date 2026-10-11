# Next release: candidate record (draft)

Prepared on October 11, 2026 for Chris to decide. Nothing here is frozen: the version, the commit and when to build
are Chris's call ([GOAL_LOOP.md](../GOAL_LOOP.md), S4). Fill in each check with the device, the commit and what was
seen; a row stays "not yet" until someone runs it. Previous release: [0.7.0](RELEASE_0.7.0.md), October 10.

## What `main` has since 0.7.0

| Change | Issue | Ships to | Checked so far | Still to check |
| --- | --- | --- | --- | --- |
| **Linux download:** the first ready-made Linux build, an AppImage (cforain's setup, #217, #226; the shared build container #254 once its image has clang 22) | #56 | Linux | Source builds by five testers; the AppImage CI | A contributor's own-disc AppImage, the release check, a Steam Deck run |
| **Faster:** the host's checks around each block of game code do less (pdale-boop, #242) | #59, #246 | Every platform | Linux: 5 to 8% on slow CPUs, identical checkpoints | The release build |
| **Faster:** returns into another chunk skip the dispatch's search, and direct calls skip the chassis for GXBegin (Elliott Tate, #234, #235) | #59 | Windows, Linux, Android builders | 811 of 811 dispatches on the prepared source; tests | The release build |
| **Faster on Linux:** draw fusion on, as on Windows (#247) | #215 | Linux | jkoehler11 on a Steam Deck: a tenth of the draws, nothing missing | Forest Haven on Linux |
| **Faster to build** on Mac, iPhone and iPad: code training never ran compiles at `-O1` (#252) | #104 | Mac, iPhone, iPad | M3 Max: 61 min of compile instead of 79; iPad Pro: the same speed in two places | PadMint's `player-check.sh` |
| Rebuilding after an app change takes about a minute (pdale-boop, #243) | none | Windows, Linux builders | Windows and Linux; Mac mods step byte-identical | none |
| Swap L and R (the triggers) under Controls (#248) | #244 | Windows, Mac, Linux | CI test | A controller |
| The developers' stage select, off by default (pdale-boop, #210, #250) | none | Windows, Mac, Linux | Played on Windows and Linux; no cost when off | The 468-place sweep on the candidate |

Not in it yet: Android (#93, waits on speed and Chris's bar), the speed sprint's changes (S1 to S3), the Linux
container (#254).

## Checks before publishing

| Check | Who | Result |
| --- | --- | --- |
| Freeze: `version.json`, the candidate commit here | Chris decides; Codex does it | not yet |
| Windows build from the candidate on Chris's PC, with the play checks in [WINDOWS_TASKS.md](../WINDOWS_TASKS.md) sections 1 and 2 | Chris's PC | not yet |
| Linux AppImage from a contributor's own disc; `check_appimage_abi.py` at 2.39; the release check | cforain or jkoehler11 | not yet |
| The app-only IPA, PadMint's `audit`, and PadMint's `scripts/player-check.sh` before tagging and after publishing | Codex, on this Mac | not yet |
| The 468-place warp sweep with the stage select | pdale-boop or Codex | not yet |
| The benchmark at Outset and Dragon Roost Cavern against 0.7.0 | Codex | not yet |
