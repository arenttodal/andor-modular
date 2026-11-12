# Addendum 02 — native ANDOR racks and the one flexible Source: report

_2026-11-12. Branch `claude/epic-feynman-fsf8ij`, on top of `5c4ee89` (the patch-workflow
pass, not redone). First pass: pins unchanged (OSP `e7a3834`, Analog `1c0cb6d`). **Second
pass** (your four answers): changes made upstream on each repository's
`claude/epic-feynman-fsf8ij`, then the pins moved to OSP `c21fe80` and Analog `40bf866`.
Nothing in a submodule was edited from here._

Built and tested in a Linux container (GCC 13 and clang 18 / libc++, headless under Xvfb)
and by CI. **Not heard through speakers, not played in a DAW, not compared by eye with your
two screenshots** (they were not available in the session: Analog's own reference renders in
`external/analog/docs/screenshots/fx-v5` were used as the visual target). What that leaves
open is at the end.

## Second pass: your four choices, built

You answered: build the upstream seam in Analog; MIX both in the racks and on the tile; live
traces for FILTER and MORPH; Modular's own preset folders.

**1. The effect modules open ANDOR Analog's own effect racks.** PHASER, FLANGER, FREQUENCY
SHIFTER, COMPRESSOR, FX FILTER, STEREO SPREAD, RETRO and CONVOLUTION now show Analog's rack
class itself: MAIN / DETAIL pages (RETRO's four), every mode (COMPRESSOR's CLEAN, FET 76,
MULTIBAND), Analog's factory presets in the rail with save / rename / delete, and Analog's
graphs metering the processor that runs in that one module (gain reduction, notches, the
comb, correlation, the impulse). Paired pictures: `docs/screenshots/parity/` (compressor,
phaser, flanger, shifter, spread, retro, convolution, fxfilter).

- *What changed in Analog* (`40bf866`, branch `claude/epic-feynman-fsf8ij`, no pull request
  opened): the rack reads its instrument through a small interface (`FxRackSource`) instead
  of casting to Analog's shell and engine; the part that needs the shell moved to its own
  file; the table of every effect's settings moved into the engine library, free of JUCE,
  and Analog's host parameters are built from it (checked name by name: identical); the user
  preset root can be set. Analog's behaviour is unchanged; its complete suites and OSP's ran
  green with these changes (see Tests).
- *What changed in the modules*: each effect module now has every setting of Analog's
  (COMPRESSOR 39 instead of 7, RETRO 47 instead of 7), from that one table. The parameters a
  module already had keep their ids and order. The patch format went to **schema 2** with a
  migration; an old patch renders bit for bit as before (measured with the previous build;
  details in STATE_SCHEMA.md).
- *Found on the way, not changed:* in Analog, FX parameters meant to be linear are skewed
  whenever their range contains -1 (the table's "no centre" marker). COMPRESSOR's THRESHOLD is
  the clear case: nearly all of its knob travel covers the top 6 dB (it is visible in Analog's
  own reference render, where -18 dB sits at the knob's minimum). Modular shows the same rack,
  so it has the same knob. Fixing it changes how hosts' recorded automation maps in Analog:
  your decision, on Analog.

**2. MIX is in the ECHO and SPACE racks, and still on the tile.** The last knob of the row,
the same parameter as the tile's. It is an opt-in of the shared rack (`c21fe80` in OSP; ANDOR
OSP and Analog do not use it and look exactly as before). In the two densest modes (ECHO
ASCEND, SPACE ABYSS) the control column grows by 11 to 19 px and the picture gives that up;
with the tape deck the reels' area is one knob narrower.

**3. FILTER and MORPH draw what plays.** FILTER reports the newest voice's cutoff, the cutoff
before the envelope, where the envelope's top takes it and the resonance as modulated; while
a note sounds its rack draws the setting quiet behind the live response, with the envelope's
travel along the foot (`rack-proc-filter-live-1448.png`). MORPH draws the two sounds'
partials as measured and, between each pair, what the method (IMPRINT / FUSE / WEAVE) makes
of them at this position. Both remain Modular's racks in Analog's identity: Analog's FILTER
rack is its whole voice and its MORPH graph is a concept view about card C (audit, 3 and 4).

**4. Presets are Modular's own.** `ANDOR/Modular/Rack Presets/` in the user data folder
(`Echo Presets`, `Space Presets`, `Movement Presets`, `FX Presets/<TYPE>`). Presets saved
earlier from Modular's ECHO / SPACE / MOVEMENT racks into the family's `Documents/ANDOR/…`
folders stay there (they are OSP's and Analog's too) and are no longer listed in Modular;
copy the files across if you want them. Factory presets are in the family's code and appear
as before.

**Not done in this pass, on purpose:** DRIVE keeps Modular's rack (Analog's has no OUTPUT);
CHORUS and TONE EQ keep an output meter (Analog has no rack for them); Analog's EQUALIZER and
UTILITY are not offered as modules; Analog's FX CHAIN presets are not offered (a chain is
Analog's route). OSP's newer default line already shares the FX library, but no Analog commit
is on that line; the seam has the same shape so the binding carries over when the pins move.

## Commits

| Commit | Gate | What |
|---|---|---|
| `bb68057` | A, B | Reuse audit; `RackHost`; the family's ECHO rack on a node |
| `3275618` | C (1) | The family's SPACE and MOVEMENT racks; the modules completed for them |
| `f86d955` | C (2) | A device rack for every other module, from the family's parts |
| `d63b386` | D, E | One SOURCE; its type changes in place |
| `7fbe25d`, `3e5ce88` | F | Warnings, gain reduction on COMPRESSOR, paired pictures, documents; a test's timing |
| OSP `c21fe80` | second pass | Opt-in MIX knob in the ECHO and SPACE racks; preset folder setter |
| Analog `40bf866` | second pass | The FX rack's seam; the parameter table in the engine library; preset root |
| this one | second pass | Pins moved; effect modules on the table (schema 2); Analog's racks bound; MIX; live traces; own presets |

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
- **The eight effects of Analog's library are Analog's rack class** (second pass, above).
- **FILTER, MORPH and DRIVE are siblings, not the same classes.** They are built from the
  same rack parts, in Analog's identity for that device (enamel, rail, motif, inks, glass),
  with the module's real controls and a picture computed from its settings and read from the
  running module. Analog's `createFilterRack` / `createMorphRack` are not used: its FILTER
  rack is its whole voice, its MORPH graph is about oscillators 1 / 2 and card C (audit,
  findings 3 and 4). This remains the gap against the addendum's wording "actual Analog
  FILTER / MORPH rack".
- A rack never touches the audio graph when it opens, swaps or closes; a control is a live
  parameter write on its node, one undo step per action (tested: no graph swap).

Paired pictures (Analog's reference above, Modular below): `docs/screenshots/parity/`
(echo-tape, echo-bbd, space, movement, filter, morph, drive, compressor, phaser). Every
device at 1448: `docs/screenshots/rack-*-1448.png`; ECHO, SPACE, MOVEMENT, FILTER and the
oscillator also at 869 and 1810 (`patch-rack-*`).

**Visible deviations in the pairs** (not hidden):
- ECHO: identical apart from the drawer's 10 px margin; Modular shows the rail's save key.
- ECHO, SPACE: one knob more than Analog's (MIX).
- FILTER: no FILTER / VOICE / PAIR pages, no filter-envelope cells (the envelope is its own
  module here); the graph is live, drawn by Modular.
- COMPRESSOR and the other effects: the same rack; the heading is the module's name
  ("COMPRESSOR", not "COMPRESSOR 1").

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

Kernel `modular_tests`: **68 cases, all passing** (2,812,009 assertions), GCC and
clang / libc++; the zero-allocation test included. Product `modular_product_tests`:
**20 cases, all passing**. Upstream, with the second pass's changes, on Linux: Analog's engine tests, OSP's plugin tests, ANDOR Example's and ANDOR Analog's product tests, every visual regression, OSP's unit, integration and regression tests: `ctest`, **13 of 13 passed** (Linux; their macOS CI was not run from here).

Second pass, by test:

| What | Test |
|---|---|
| Every setting of Analog's table is a parameter; the old ones keep their place | kernel `[fx][table]` |
| The new settings at their defaults leave the sound as it was | kernel `[fx][table][regression]`; old vs new build, sample by sample (STATE_SCHEMA.md) |
| Schema 1 → 2: mappings, group controls, modulation depths, scenes, FET input | kernel `[state][fx]`, fixture `tests/fixtures/patch-v1-fx.json` |
| Analog's rack on a module: every ID bound, pages, one undo, no graph swap, presets, meters, two compressors independent, each its own memory | product `[rack][native][fx]` |
| All eight effects, every mode and page, every factory preset; CONVOLUTION's impulse | product `[rack][native][fx]` |
| MIX in ECHO (3 machines, free / sync, tape deck) and SPACE (4 engines): fits, bound both ways | product `[rack][native][live]` |
| FILTER's live trace; MORPH's methods | product `[rack][native][live]` |
| Analog: the table; the rack through `FxRackSource` alone | Analog `[fx][parameters]`, `[analog][fx][rack][seam]` |

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

1. **FILTER, MORPH and DRIVE are siblings built from the family's parts, not Analog's rack
   classes** (above). The eight library effects are Analog's class.
2. **CHORUS and TONE EQ show an output meter**, not a picture of their own (the meter is real).
3. **A group still opens the plain rack.**
4. **A deep modulation on one of ten widened parameters** (schema 1 patches): where it used
   to stop at the old range's end it now carries on to the new end (STATE_SCHEMA.md).
5. **COMPRESSOR's THRESHOLD knob is strongly skewed**, as in Analog (second pass, above).
5a. **A rack's page and shown preset name are remembered for the session**, per module, not
   saved in the patch. SHAPER's drawn patterns stay in the family's shared pattern folder.
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

The four from the first pass are built (above). New, from the second pass:

1. Analog's skewed THRESHOLD (and the milder cases): fix it in Analog? It changes how recorded
   automation of those parameters plays back there.
2. Open pull requests for the upstream branches (`claude/epic-feynman-fsf8ij` in OSP and in
   Analog)? None was opened; Modular's pins point at those branches' commits.
3. EQUALIZER and UTILITY as modules (Analog's racks would open as they are)?

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

Second pass:

10. COMPRESSOR beside ANDOR Analog's: the same rack? CLEAN, FET 76 and MULTIBAND each act;
    DETAIL's controls act; "FET Punch" and "OTT" load; GR moves with the playing. Two
    compressors: different settings, different meters.
11. RETRO: the four pages; TAPE, VINYL, DIGITAL (80s / 90s / CODEC) sound right.
    CONVOLUTION: REVERSE and SIZE rebuild the impulse without a click you mind.
12. ECHO and SPACE: is MIX where you expect it; does the tape deck still read well with one
    knob more?
13. FILTER with an envelope plugged in: does the live trace show what you hear? MORPH with
    two sources: do the bars between A and B read as the result?
14. Save a preset in an ECHO rack and a COMPRESSOR rack; find the files under
    `ANDOR/Modular/Rack Presets`; they do not appear in ANDOR Analog.
15. A patch of yours from before today with a phaser, flanger, compressor or convolution and
    a macro or LFO on it: same sound?
