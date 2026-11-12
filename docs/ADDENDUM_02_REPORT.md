# Addendum 02 — native ANDOR racks and the one flexible Source: report

_2026-11-12. Branch `claude/epic-feynman-fsf8ij`, on top of `5c4ee89` (the patch-workflow
pass, not redone). Pins unchanged: OSP `e7a3834`, Analog `1c0cb6d`; nothing in either
submodule was edited._

Built and tested in a Linux container (GCC 13 and clang 18 / libc++, headless under Xvfb)
and by CI. **Not heard through speakers, not played in a DAW, not compared by eye with your
two screenshots** (they were not available in the session: Analog's own reference renders in
`external/analog/docs/screenshots/fx-v5` were used as the visual target). What that leaves
open is at the end.

## Commits

| Commit | Gate | What |
|---|---|---|
| `bb68057` | A, B | Reuse audit; `RackHost`; the family's ECHO rack on a node |
| `3275618` | C (1) | The family's SPACE and MOVEMENT racks; the modules completed for them |
| `f86d955` | C (2) | A device rack for every other module, from the family's parts |
| `d63b386` | D, E | One SOURCE; its type changes in place |
| this one | F | Warnings, gain reduction on COMPRESSOR, paired pictures, documents |

## Part A: what opens when a module is double-clicked

Every module of the patch now opens a 214 px ANDOR device in the drawer (the plain cream rack
remains only for a group). The map, with how much of each is the family's own code, is in
[ADDENDUM_02_REUSE_AUDIT.md](ADDENDUM_02_REUSE_AUDIT.md), section 4. In short:

- **ECHO, SPACE, MOVEMENT are the very racks of OSP and Analog** (`EchoRack`, `SpaceRack`,
  `MovementRack`, unmodified), in Analog's faceplates: the tape deck, the repeats graph, the
  decay portrait with its cut handles, the pattern editor, the preset browsers. Each is bound
  to one node through `RackHost`; two echoes never share a control (tested).
- **So that nothing in those racks is a dead control, the modules were completed** with what
  ANDOR's own have (the shared processors, at their defaults, so old patches sound the same):
  ECHO's ASCEND interval and motion; SPACE's VINTAGE and BLOOM engines and ABYSS's resonance,
  freeze and variant; MOVEMENT's SHAPER with library and drawn patterns.
- **FILTER, MORPH, DRIVE and the effects are siblings, not the same classes.** They are built
  from the same rack parts, in Analog's identity for that device (enamel, rail, motif, inks,
  glass), with the module's real controls and a picture computed from its settings or read
  from the running module. Analog's own `createFilterRack` / `createMorphRack` /
  `createFxRack` are **not** used: they cast to Analog's shell and engine, and its FILTER rack
  is its whole voice (audit, findings 2 to 5). This is the main gap against the addendum's
  wording "actual Analog FILTER / MORPH / effects rack"; the upstream seam that would close it
  for the effects is described in the audit and was not built.
- A rack never touches the audio graph when it opens, swaps or closes; a control is a live
  parameter write on its node, one undo step per action (tested: no graph swap).

Paired pictures (Analog's reference above, Modular below): `docs/screenshots/parity/`
(echo-tape, echo-bbd, space, movement, filter, morph, drive, compressor, phaser). Every
device at 1448: `docs/screenshots/rack-*-1448.png`; ECHO, SPACE, MOVEMENT, FILTER and the
oscillator also at 869 and 1810 (`patch-rack-*`).

**Visible deviations in the pairs** (not hidden):
- ECHO: identical apart from the drawer's 10 px margin; Modular shows the rail's save key.
- FILTER: no FILTER / VOICE / PAIR pages, no filter-envelope cells (the envelope is its own
  module here); the graph is the response at rest, not Analog's live per-voice trace.
- COMPRESSOR and the other effects: fewer controls and modes than Analog's (the module's
  parameters), no preset browser, no MAIN / DETAIL pages.

## Part B: one SOURCE

- **Quick Add → Sources → SOURCE**: an oscillator, wired and audible at once (the same
  immediate-add path, asserted under 100 ms). "source", "oscillator" and each type's own
  words ("organic", "sample", "granular", "rhythm", "kontakt", "noise", "input") find it as
  that type.
- **TYPE on the card, the tile's menu and the rack**: ANALOG (CLASSIC / DCO / WIDE are its
  model, inside it), ORGANIC, GRANULAR, RHYTHM, EXTERNAL, NOISE, LIVE INPUT. One selection.
- **Stage 1 architecture (as the addendum prefers):** the type is a presentation over the
  existing modules. `edit::changeSourceType` changes the node's type in place; no wrapper node
  was introduced, no type id renamed, and every factory file loads and saves byte for byte
  (tested).
- **What a change keeps** (tested in the kernel and through the editor): the node's id, name,
  place, group, mute and solo; the left type's settings, recording or plugin, and where it
  ran; cables and macro / host assignments the new type cannot honour are set aside with the
  type they belong to and come back with it; a cable any earlier type set aside is taken back
  as soon as a type can use it.
- **What a change does to the routing, and says so in the status line:** a type that plays
  once for all notes (RHYTHM, EXTERNAL, LIVE INPUT) cannot feed a per-voice chain; it joins
  where that chain's voices meet. Going back restores the per-voice chain. A type that cannot
  fit, or breaks a limit (a second hosted instrument), is refused with the reason; nothing
  changes.
- One undo step back and one redo forward are exact (document text compared).
- Under a held chord: no stuck note, no NaN, the chord still sounds when the type returns
  (tested through the engine). The swap is the engine's existing click-free graph swap; it
  was not listened to.

## Tests

Kernel `modular_tests`: **63 cases, all passing** (1,970,974 assertions), GCC and
clang / libc++; the zero-allocation test included. Product `modular_product_tests`:
**18 cases, all passing**.

| Requirement (addendum part D) | Test |
|---|---|
| 1, 2 ECHO is the family's rack; two echoes independent; no graph swap; FEEDBACK changes the tail | `[ui][rack][native]` "ECHO opens ANDOR's own ECHO rack…" |
| 4 SPACE / MOVEMENT: every engine and mode the rack offers plays in the module | `[ui][rack][native]` "SPACE and MOVEMENT…" |
| 3, 5, 7 every module opens a device; cells never spill, on every page | `[ui][rack][native]` "Every module opens a device of its own" ; `[ui][rack]` FILTER's controls, ON, units |
| 6 X / Escape; no rebuild on open | `[ui][rack]`, `[ui][escape]` (unchanged, still passing) |
| 8 Quick Add SOURCE, immediate | `[ui][source][type]` |
| 9, 10, 11 each type sounds; settings, recording, model come back | kernel `[source][type]`, product `[ui][source][type]` |
| 13 name, M / S, six sources, no duplication | product `[ui][source][type]` |
| 14 shared routing after a type change | kernel `[source][type][routing-domains]` |
| 15 factory patches unchanged, every source shows its type | product `[source][type][state]`; kernel FactoryTests |
| 16 undo / redo exact | product `[ui][source][type]` |
| 17 no allocation on the audio thread | `RealtimeTests` (every factory patch) |
| 18 Escape, scrolling, layout, shared filter, M / S | the previous pass's cases, all still passing |
| 12 EXTERNAL with Kontakt | **not testable here** (no plugin installed); the limit and the type change are tested with the surrogate |

## Remaining limits (precise)

1. **Effects, FILTER and MORPH are siblings built from the family's parts, not Analog's rack
   classes** (above). Consequence: fewer controls than Analog's effect racks, no effect preset
   browsers, FILTER's graph is not the live per-voice trace.
2. **SHIFTER, CHORUS, CONVOLUTION and TONE EQ show an output meter**, not a picture of their
   own (the meter is real).
3. **A group still opens the plain rack.**
4. **The amount of ECHO / SPACE is the module's MIX on its tile**, not a control inside their
   racks (in Analog it is the macro above the rack).
5. **The ECHO / SPACE / MOVEMENT preset browsers use the family's folders**
   (`Documents/ANDOR/… Presets`), so presets are shared with OSP and Analog on that machine;
   the browser reads the folder while the rack is open.
6. **Scene values captured for a source follow its parameter names across types** (a scene's
   LEVEL applies to whichever type is active); scenes are not kept per type.
7. **A type change rebuilds the graph** (it is structural). The swap is click-free by the
   engine's design and tested for stuck notes, but a recording or a plugin still loads in the
   background first: the card says LOADING, then plays.
8. **LIVE INPUT is offered even when the host gives no input** (it is then silent).
9. **Narrow windows:** the instrument scales as one piece, so a rack is never re-flowed; at
   869 px the cells are small (see the 869 screenshots).
10. Undo of mute / solo / bypass still rebuilds (previous report).

## Your choices

1. Build the upstream seam in Analog (audit, section 4) so the effect modules open Analog's
   own effect racks with all their controls and presets? It is two contained changes in
   Analog plus exposing the settings in Modular's effect modules.
2. MIX inside the ECHO / SPACE racks (a small opt-in in the shared rack), or keep it on the tile?
3. Should the two sibling racks you named first, FILTER and MORPH, gain a live trace
   (per-voice cutoff, envelope) like Analog's? The module would need to report it.
4. Preset folders: share the family's, or give Modular its own?

## Hands-on checks (not verifiable here)

On a Mac, in a DAW, with the installer from the latest green CI run:

1. Double-click ECHO beside ANDOR Analog's ECHO rack: same faceplate, reels, graph, presets?
   FREE / SYNC, MONO / PING / WIDE and TAPE / BBD / ASCEND all act on the sound?
2. Two ECHO modules: different settings, open one then the other while a note rings; nothing
   jumps, the tails carry on.
3. SPACE: ROOMS, ABYSS, VINTAGE, BLOOM each sound right; a preset loads; LOW / HIGH CUT
   handles in the graph work. MOVEMENT: SHAPER's playhead follows the transport; draw a pattern.
4. FILTER, MORPH, DRIVE, COMPRESSOR, an envelope, the mixer, the step sequencer: is each
   device clear without reading its title; anything clipped or cramped at the smallest size?
5. New patch → right-click → Sources → SOURCE → play. On PLAY, TYPE → ORGANIC, drop a
   recording, play; TYPE → ANALOG, choose DCO; TYPE → ORGANIC: your recording is back.
6. TYPE → RHYTHM on a source in front of a filter and envelope: read the status line; the loop
   plays; TYPE → ANALOG: the filter and envelope shape it again.
7. TYPE → EXTERNAL, choose Kontakt from the rack's rail, open its window, play; save and
   reopen the DAW session.
8. Change a source's type while holding a chord with an arpeggiator running: any click, any
   stuck note?
9. Load each factory patch and one of your own from before today: same sound, same routing.
