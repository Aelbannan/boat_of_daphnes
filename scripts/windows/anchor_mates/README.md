# Scripted Anchor mates for the puppet scenarios (testing only)

- `run_mates.sh <room> <record s> <total s> <set>` starts `server/anchor/test/pose_replay.mjs` mates against a local server
  on port 8811 (`npx wrangler dev --port 8811 --local` in `server/anchor`). Sets: A (Link only, equipment only, stock, voice
  only), B (one mod's Link, equipment and voice, stock, a voice), C (a mix of three mods), L (an older client), M (many custom
  players: the room limit), S (one stock player), V (three voices), W (the sharing scenarios). The mod folders are extracted
  wwrando models (Link.arc, sound\) in `build/mate_mods` (or `BOD_MATE_MODS`): Young, Samus, Vaati, Aryll, Goku. Mods stay in
  that gitignored folder and are never committed.
- `go_group.sh <scenario> <tag> <room> <set> [scenario.py args]` runs the mates, then the scenario (`run_sc.sh`; set `BOD_MODULE`
  to the game module), and stops the mates of port 8811 afterwards. The log is `build/shots/sc_<tag>.log`.
- `afc_map.py <run.log> <JaiInit.aaf> <voice_0.aw>` (the game's own bank, from your disc) prints, from a voice sweep (`anchor_parts_sweep` with `--env BLUEWAKE_DSP_HLE_AFC_TRACE=1`), the wave of
  the vanilla voice bank each voice id played (the table in `windows/src/voice_wave_map.inc`). It reads the disc's
  `JaiInit.aaf` and `voice_0.aw` from a folder set in `SCR` inside the script.
