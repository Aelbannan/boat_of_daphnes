# Picto Box recognition, October 15, 2026

BlueWake #334 (jslay88, `bac7fe3`) and RecompCore #36 (`5423b26`, merged as
`57123c6`) answer the game's actor-id read from the current EFB. Previously every
`GXPeekARGB` read returned zero, crediting pixels to the first registered subject.
The host drains pending FIFO work before the first read; the runtime takes one
snapshot for that frame segment and serves the remaining reads from it.

## Local Mac check

M3 Max, macOS 26.6.2, Metal, 1920 × 1440 framebuffer, original 4:3 view,
Smooth Motion off. A copied card and isolated settings/cache were used; stage
select disabled in-game saving. The card was byte-identical after every run.

The game's photo judgment was observed with temporary local diagnostics. Those
hooks, private states, card, image buffers and module are not part of the PR.

| Shot | Result |
| --- | --- |
| Lenzo centered in his studio, normal rendering and warm shader cache | Subject 94, detail 0: Lenzo recognized |
| Turn to the empty wall and take another photo | Subject 0: no subject recognized |
| Repeat the pair with synchronous pipelines as a diagnostic | Same results |

This is recognition evidence from actual gameplay. It is not a local completion
of the three-photo Lenzo quest, nor a widescreen or mobile check.

## Contributor checks

- jslay88: Lenzo accepts the letter photo on Linux ([#333](https://github.com/chrissotraidis/bluewake/issues/333));
  also confirmed the fix on an M4 Pro Mac ([#334](https://github.com/chrissotraidis/bluewake/pull/334#issuecomment-6163798355)).
- pdale-boop: all three Lenzo quest photos accepted on Windows, with deliberate bad
  photos rejected ([#334](https://github.com/chrissotraidis/bluewake/pull/334#issuecomment-6159163583)).

## Remaining first-use follow-up

An isolated empty shader cache exposed a pre-existing renderer behavior: draws
can be omitted while their pipelines compile. That includes the alpha-only clear
and actor-id pass, so the first readback can contain ordinary scene alpha.

[RecompCore#39](https://github.com/chrissotraidis/RecompCore/pull/39) waits for
alpha-only pipelines, keeping ordinary color draws asynchronous. On the Mac it
produces the expected alpha values on the first cold-cache photo and passes the
Lenzo/empty-wall pair. It is **not in this pin**: Windows/Linux gameplay checks
remain before adopting the follow-up. The source-only integration CI does not
replace those checks. iPhone/iPad and Android recognition remain unchecked.

Issue #267 stays open for its reporter. Photo cropping/gallery shape (#338 and
RecompCore#37) is separate from actor recognition.
