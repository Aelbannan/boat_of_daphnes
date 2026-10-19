# Translated chunk investigation

Supports M0-T02/M2-T01; desktop candidate investigation, not a retained change.

## What the profile establishes

Baseline `3e82deb`, module from `build/desktop-m0-baseline/BlueWake/`:
`func_802456E0` accounts for 5.30%/5.41% of process CPU cycles in the two rendered
captures and 7.69% headless. The headless annotation contains 230 samples for this
symbol, distributed over many instructions. The ELF symbol is 949,253 bytes long.
The generated entry table covers 4096 guest addresses, `0x802456E0–0x802496DC`.
This symbol must not be interpreted as one original game function.

## Mapping limits and leads

`addr2line` identifies the generated C file but returns no source line. Reading
the ELF jump table provides nearby native block-entry addresses; compiler block
reordering, shared tails and cloned paths mean nearest-entry lookup is only a
lead, not proof that a sampled instruction executes that guest instruction.

| Sampled native address | Nearby guest entry | Generated guest operation / context |
|---|---|---|
| `0x2981c21` | `0x80246954` | Load through r28, followed by another pointer load |
| `0x2981a1d` | `0x802469E8` | Increment loop index; compare with 256; advance r28 by 20 |
| `0x2981e9a` | `0x80247368` | Byte load and comparison |
| `0x2965616` | `0x80249240` | Pointer load and scaled index |
| `0x2986e81` | `0x80249268` | Floating-point load |

These operations come from the private generated translation and are not a
verified identification of the corresponding original game routines. The local
checkout has no `config/GZLE01/symbols.txt`, despite the profiling guide referring
to it. Do not infer routine names from nearby addresses.

## Existing native coverage

[`native_game_math.c`](../../../cmake/composite/native_game_math.c) already
recognizes vector add/subtract/scale entries `0x80245674`, `0x802456C4`, and
`0x80245714`. The first two precede this chunk's entry range; chunk boundaries
can split original functions. The other regions above are not those three
entries. The sampled `bluewake_native_game_math_try` symbol also contains inlined
native computation; its entire cost cannot be attributed to readiness checks.

## Next gate

Build a diagnostic copy of this translation unit with line tables, retaining the
baseline optimization flags and PGO, then collect a fresh profile of that exact
binary. Existing sample addresses must not be applied to a rebuilt binary.
Use exact source attribution and a verified game-symbol map before deciding on
a native replacement or changes to loop/state bookkeeping. No runtime change is
justified by nearest-entry evidence alone; no game-speed benefit is claimed.

Local evidence: `profiling/desktop-cost-01/` under the BlueWake workspace parent.
Commands used: `perf annotate --stdio --symbol func_802456E0`, `nm -S`,
`addr2line -f -i`, ELF program-header/jump-table inspection, and generated-source
inspection. The saved-state mod mismatch and desktop-only scope still apply.
