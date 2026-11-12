# Patch workflow, audio reliability, shared routing and material pass — report

_2026-11-12. Branch `claude/epic-feynman-fsf8ij`. Started from `d91abfc`. Pins unchanged: OSP
`e7a3834`, Analog `1c0cb6d` (nothing in either submodule was edited)._

Everything below was built and tested in a Linux container (GCC 13, headless under Xvfb) and
by CI on Linux and macOS 15. **Nothing here was heard through speakers, played from a DAW or
touched with a trackpad by a person.** What that leaves open is the checklist at the end.

## Commits

| Commit | Gate | What |
|---|---|---|
| `d5626a1` | 0–2 (P0) | Silent ORGANIC SAMPLE, hanging notes and clicks on KEYBOARD OCTAVE, clicks on mute / bypass; real Mute and Solo |
| `3b483b1` | 3 (P0) | Several sources into one filter or effect |
| `83cdf32` | 7 (P1) | The layout rule (kernel, pure) |
| `4a277b6` | 4–6 (P1) | Compact Quick Add, one Escape stack, the rack, natural scrolling, F removed, layout applied to every edit |
| `363d551` | 8 (P2) | Ebonized ash and black anodized aluminium |
| this one | 9 | Make-room glide, timing checks, listening demos, documents |

## Audit: what was wrong, shown by a failing test first

### ORGANIC SAMPLE sometimes silent
| Cause | Evidence | Fix |
|---|---|---|
| **A recording with no clear pitch** (speech, texture, drums). OSP's analysis reports a root even when it has no confidence in one: MIDI 101 at 6 % for the probe. The keys then played the recording three to four octaves down, below hearing, from a source that said READY | `[organic][live]` "no clear pitch": 23 zero crossings per second played against 682 recorded, on the old code | Under OSP's "moderate" confidence (0.4) the root is C4 when ROOT is automatic (`SampleAsset::pitched`): middle C plays it as recorded. ORGANIC SAMPLE and GRANULAR. The card says "NO CLEAR PITCH: C4 PLAYS IT AS RECORDED" |
| A sample source added from Quick Add had no recording: silent until a file was dropped on it | product test "added from Quick Add sounds at once" | It starts with one of ANDOR's own recordings (same undo step) |
| START and REVERSE were ignored by the first note after any graph build | `[organic][live]` "START and REVERSE" | Read from the compiled parameters in `prepare` |
| _Not_ causes (tested, kept as guards) | a recording arriving under a held key; one voice, mono, legato; a one-shot that ended; paths and names beyond ASCII | — |

A wrong first hypothesis (non-ASCII paths) was tested, found false and backed out before it
was committed.

### Hanging notes and clicks when KEYBOARD OCTAVE moves
Any setting of a note module rebuilt and swapped the whole graph (held notes restarted an
octave away, effect tails reset: the click), and a note-off was transposed with the settings of
its own moment (through an ARPEGGIATOR the held chord never ended; a key moved out of range
never released). Now note modules are read live, and KEYBOARD, KEY SPLIT and SCALE / TRANSPOSE
remember what each note became (`NoteMemory`): a note-off ends exactly that note. **Policy:**
held keys keep their pitch; the new octave is for the next notes. Tests
`[event][transpose][lifecycle]` (four cases; they fail on the old code).

### Clicks on mute and bypass; no Solo
The light on a tile and M on a card toggled `enabled` through a full rebuild. Now bypass
(`CompiledNode::live`) and audibility (`CompiledNode::gain`) are set in the running graph and
ramped over 12 ms. Mute and Solo are their own saved state (`muted`, `solo` on modules and
groups; written only when set, so schema 1 files are unchanged): a source is faded at its own
output, before anything downstream. Mute wins over solo; Shift-click adds to the solo; a SOLO ×
control appears in the header only while something is soloed. Tests `[source][mute][solo]`
(ramps at 44.1 / 48 / 96 kHz and blocks of 16 / 64 / 512: no rebuild, no step larger than the
signal's own, no restarted note).

### Several sources into one filter or effect
Audio inputs of processors, effects and utilities take several cables (summed). Dragging a
cable goes through `edit::connectSmart`:

| Case | What happens |
|---|---|
| A. Sources of one note stream → a per-voice processor | Connected and summed per voice: one filter per voice, never a copy per source |
| B. Chains with their own ARPs → one processor | The processor is moved after the voice mix, set GLOBAL, and each other chain reaches it through a VOICE MIX of its own: it runs once; each chain keeps its notes |
| C. Parallel | As before (a source can feed several things) |
| D. MORPH | Keeps its two inputs apart; a second cable into A or B is refused with the reason |

Tests `[routing][fanin]` and `[routing-domains]`.

### Escape
Each component handled Escape for itself, in an order that depended on focus. Now one function
(`Editor::dismissTopmost`) closes exactly one thing per press: a drag under way (a move goes
back where it started) → Quick Add's submenu → Quick Add → EVOLVE → the rack → the group being
edited → the selection. Nothing reaches the history. A parameter's popup menu is modal and
takes its own Escape first. Test `[ui][escape]` checks which thing closed at each press, by
method and through the keys.

### Scrolling reversed
`Canvas::mouseWheelMove` multiplied the deltas by `MouseWheelDetails::isReversed`. The system
has already applied the user's direction to the deltas; the flag only reports it. Multiplying
by it reversed natural scrolling a second time. Removed. MORE → Board scrolling → Traditional
flips it for those who want that (a preference file, never the patch). Test `[ui][gesture]`.

### Modules landing on each other
Placement was per action and did not look at what was there. `edit::resolveOverlaps` is one
pure rule run inside every edit's undo step and once on load: what the edit just placed stays;
others move by the least it takes, in a fixed order, on the 8 px grid. The tiles that make room
glide there in 160 ms (the picture only: the patch is final at once).

## What changed for the musician

- **Quick Add** is a small menu at the pointer (268 px wide): search, up to three recent
  modules, six categories; a category opens beside it on hover (90 ms; 170 ms when moving
  between categories) or with Right. Typing gives up to eight matches. Up, Down, Left, Right,
  Return, Escape. On a port or cable only what fits. One Return, one module.
- **The rack**: double-click a module (or Return, or a PLAY card's key): its controls open in
  one drawer across the foot of the board (260 px), identity left, modes and knobs centre,
  live display right. The board keeps its width, zoom and place. Another module swaps in. X or
  Escape closes. Double-clicking a group still opens the group.
- **F** is only a letter. **FIT** is the button ("Fit patch"). **0** is still 100 %.
- **M and S** on source and group tiles and on PLAY's cards.
- **The finish**: docs/MATERIALS.md.

## Measurements (this container; a shared VM)

| | Target | Measured |
|---|---|---|
| Quick Add visible | < 50 ms | 0.1 ms to open and lay out (test asserts < 50) |
| Quick Add → playable graph | < 100 ms | asserted by the existing product test |
| Rack open | immediate, no audio change | 0.4 ms; no graph swap (asserted) |
| Layout, 32 modules stacked on one spot | inside one frame | 0.6 ms worst of 5 (asserts < 8) |
| Make-room glide | 120–200 ms | 160 ms, ease-out |
| Mute / solo / bypass ramp | 10–20 ms | 12 ms |
| Heap calls on the audio thread | 0 | 0 (`RealtimeTests`, every factory patch) |
| Overlapping tiles after an edit | 0 | 0 (property test, 40 seeds; every factory patch; product test through the menu) |

## Test results

Kernel `modular_tests`: **59 cases, all passing** (1,689,055 assertions), plus the hidden
`[.demos]` case. Product `modular_product_tests`: **13 cases, all passing** (444 assertions),
including screenshots. CI: Linux and macOS green for `d5626a1` and `3b483b1`; see STATUS.md
for the later commits.

| Area | Tag | Cases |
|---|---|---|
| Silent recordings | `[organic][live]` | 4 kernel, 1 product |
| Octave / range / split / scale lifecycles | `[event][transpose][lifecycle]` | 4 |
| Mute, solo, bypass | `[source][mute][solo]` | 2 kernel, 1 product |
| Shared routing | `[routing][fanin]`, `[routing-domains]` | 3 |
| Layout | `[ui][layout]` | 3 kernel, 1 product |
| Quick Add | `[ui][quickadd]` | 1 product |
| Escape | `[ui][escape]` | 1 product |
| Rack | `[ui][rack]` | 1 product |
| Scrolling, F | `[ui][gesture]` | 1 product |
| Finish | `[ui][materials]` | 1 product |

## Listening demos

`tools/render-demos.sh BUILD OUT` writes the factory renders and, in `OUT/patch-workflow`:

1. `1-two-sources-one-voice-filter.wav`: two oscillators of the same notes through one
   per-voice filter.
2. `2-two-arps-one-global-filter.wav`: two arpeggiated chains through one filter that runs once.
3. `3-unpitched-recording-played-c4-then-c5.wav` (and its source): the silent case, audible.
4. `4-octave-moved-under-a-held-chord.wav`: the chord stays and ends; the next ones are higher.
5. `5-mute-and-solo-under-a-held-note.wav`: M and S, live.

They prove the routing and the absence of dropouts; whether they sound _right_ is a listening
judgement.

## Known limits of this pass

- **Undo / redo of a mute, solo or bypass rebuilds the graph** (the action itself does not).
  Held notes survive a rebuild, but it is not the 12 ms ramp. Bypassing a _note_ module is
  still a rebuild.
- **The rack's display for a filter is its output level, not a response curve.** Sources show
  the front panel's display; arpeggiators, sequencers, morphs and the satellite show theirs;
  everything else a level or a value. A drawn filter response is not built.
- **The rack is not saved with the patch** (it is closed when a patch opens).
- **Quick Add covers the instrument while open**: a click outside closes it and is not passed
  on to what is under it (a second click is needed there).
- **Scroll direction was fixed by reading the code path, not by hand on a Mac.** The test
  drives the same event path with the flag both ways.
- **The finish was built without seeing the Concept 2 image** (docs/MATERIALS.md).
- Windows is still not built in CI.

## Owner decisions

1. Octave policy: held keys keep their pitch (chosen). The alternative (retune held notes) is a
   different instrument; say if you want it.
2. Rack height (260 of the board's 716 px) and whether a filter response display is worth a pass.
3. The live accent under the new trim (kept copper).
4. Whether the two shared-filter patches from the demos should become factory patches.

## Owner verification checklist (not verifiable here)

On a Mac, in a DAW (Logic, Live or Reaper), with the installer from the latest green CI run.
Mark each: pass / fail / note.

1. **Organic recording.** New patch → Quick Add → "organic" → Return → play: ANDOR's bowed
   recording sounds. Drop a spoken-word or drum recording on it: C4 plays it as recorded, the
   card says NO CLEAR PITCH. Drop a clearly pitched one: keys play in tune. Save the DAW
   session, reopen: it still sounds.
2. **Mute / Solo.** "Six Oscillator Stack", hold a chord: M on a tile and on a PLAY card (no
   click, the note carries on); S alone, S with Shift for two; M wins over S; SOLO × in the
   header ends all solos; groups ("Bowed Machine") mute and solo as one.
3. **One filter for several sources.** Two oscillators on one keyboard: drag the second's OUT
   onto the filter's IN: the status line says "2 sources", the socket shows 2, the cutoff
   darkens both.
4. **Arps into one filter.** "Three Arps": drag a second chain's OUT onto the first chain's
   filter: the status line says it runs once; each arp keeps its own pattern; no stuck notes
   after release.
5. **Octave under held notes.** Hold a chord, turn KEYBOARD OCTAVE: no click, no jump, the
   chord ends on release; the next chord is at the new octave. Repeat through an ARPEGGIATOR.
6. **Layout.** Insert MORPH on a cable between two close modules; add three sources next to a
   filter; drag a tile onto another: nothing overlaps, what you placed stays where you put it,
   the others glide aside; one undo restores everything.
7. **Rack.** Double-click FILTER: the drawer opens under the board, nothing on the board
   jumps. Turn RESONANCE, change the model. Double-click SPACE: swapped. Escape: closed.
   Right-click a rack knob → assign to a macro: works as on the tile.
8. **Menu and Escape.** Right-click the board, rest on Spatial, Escape: only the submenu
   closes; Escape again: the menu. Reopen, type "echo", Return: ECHO added once. Try it near
   the window's right and bottom edges (the menu stays inside), and press Escape many times
   quickly: nothing else closes that should not.
9. **Navigation.** Magic Mouse and trackpad, with macOS "natural scrolling" on and off: the
   board follows your fingers the way other apps do; pinch zooms about the pointer;
   Cmd + scroll zooms. Press F: nothing moves. Click FIT: the patch fits. MORE → Board
   scrolling → Traditional reverses it and is remembered after reopening.
10. **Finish.** At the smallest, the default and the largest window size, on a Retina and a
    non-Retina display, beside the Concept 2 image: cheeks almost black with visible grain and
    no walnut warmth, rails and caps satin black, fasteners discreet, the face and the modules
    as readable as before.
11. **Stress.** While a chord and an arp play: add and remove a dozen modules, switch factory
    patches, open and close racks, M / S, change the octave, undo and redo a lot. No stuck
    notes, no dropouts, no crash, no menu left behind.
12. **Recall.** Save the patch and the DAW session with M / S set, a recording loaded and
    macros assigned; quit; reopen: topology, recording, M / S, macros, positions and sound are
    back (the rack is closed by design).
