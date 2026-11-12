# Addendum 02 — reuse audit: the family's racks and source cards

_2026-11-12. Modular `5c4ee89` + this work. Pins read: OSP `e7a3834`, Analog `1c0cb6d`._

What Modular can show of ANDOR OSP's and ANDOR Analog's racks **as the very same code**, what
is tied to their instruments, and what the bridge provides. Read from the source; file and line
references are to the pinned submodules.

## 1. How the family's racks are built

| Layer | Where | What it needs |
|---|---|---|
| The rack's parts: `rack::Module`, `rack::Host` (one module at a time, the collapse X, full bleed), `ParameterRack` (rail, pages, selector row, one row of cells, the graph's glass), `rackui` cells (`MiniKnob`, `SegmentedSwitch`, `VerticalSwitch`, `ValueSelector`, `PowerKey`), `rack::Faceplate` + `hardware::paint` | OSP `shared/andor-ui/Source/{EffectRack,ParameterRack,RackControls,RackFaceplate,ShapingPopups}.*` | an `osp::plugin::InstrumentHost&`: every cell attaches to a parameter **by ID** in the host's `AudioProcessorValueTreeState` (a missing ID is dereferenced: it must exist) |
| ECHO, SPACE, MOVEMENT | OSP `MacroPopups.cpp`: `EchoRack`, `SpaceRack`, `MovementRack` on `UnifiedRack`; `createRackModule (rack::Kind, InstrumentHost&)` (:5769) | the ANDOR parameter schema (`InstrumentHost::createParameterLayout`), `hostTempo()`, `outputPeak()` (the tape reels turn while something sounds), `rackFaceplate (kind)`, `rackPresetMemory`, for MOVEMENT `movementLane` / `laneShaperPhase` / `laneShaperPattern`. **Nothing of an engine.** Graphs are computed from the parameters and the tempo |
| Device identities (enamel, rail, motif, inks, glass) | Analog `product/Source/FxIdentity.*`: `visualIdentityFor (fx::Type)`, `synthIdentity`, `faceplateFromIdentity` | nothing (already compiled into Modular for the tiles) |
| FILTER, MORPH | Analog `product/Source/AnalogRacks.cpp`: `createFilterRack`, `createMorphRack` | Analog's whole voice schema (`analog.*`, `params::Reader`), and `FilterGraph` casts the host to `andor::shell::ShellProcessor` and its engine to `AnalogEngine` (:156) for `filterTap`, `cutoffNoteHigh`; `MorphGraph` asks `engineStatus ("morph")` about card C |
| The effects library (SPREAD, COMPRESSOR, EQ, FX FILTER, PHASER, FLANGER, SHIFTER, CONVOLUTION, UTILITY, RETRO, DRIVE) | Analog `product/Source/FxRack.cpp`: `createFxRack` | `fxparams` IDs (`analog.fx.<type>.<n>.<name>`), `engineOf (host)` (:35: reference casts to `ShellProcessor` and `AnalogEngine`, which throw on any other host) for the live processor's meters (:228, :908) and the convolution's impulse (:659); `openFxRack (ShellEditor&)`; the process-wide `shownInstance` |
| Source cards | OSP `EngineCard.*`, `MainSections.*`; Analog `SampleSourceUI.cpp`, `AnalogWellOverlays.cpp` | layers A / B / C of one instrument (`layerParameterId`, `currentInstrument (layer)`, `generatedSource (layer)`), three fixed cards |

## 2. Findings

1. **ECHO, SPACE and MOVEMENT are reusable as they are.** They touch only the host interface.
   Modular needs an `InstrumentHost` whose parameter state *is one module*: `RackHost`
   (product/Source/RackHost.*). No upstream change.
2. **"Just call Analog's factories" does not work for FILTER, MORPH and the effects library.**
   `createFxRack` and the FILTER graph throw `std::bad_cast` on a host that is not Analog's
   shell, and compiling `FxRack.cpp` / `AnalogRacks.cpp` drags `AnalogEngine.cpp`,
   `ShellProcessor.cpp` and `ShellEditor.cpp` into the link (typeinfo and out-of-line calls).
   Modular constructs no `AnalogEngine` and casts to no shell. These racks need a small seam
   upstream (section 4).
3. **Analog's FILTER rack is Analog's voice, not a filter module.** Its three pages hold the
   amp, unison, LFO routing, chorus, oscillator pair and poly-mod of Analog's synth: none of
   that is in Modular's FILTER node. What carries over is its identity, its FILTER page's
   controls that exist here, and its response maths (`filtergraph::responseDb`).
4. **Analog's MORPH rack speaks of oscillators 1 / 2 and card C** (CARRIER, ROOT of card C,
   its empty state). In Modular MORPH is a node with inputs A and B. Its identity, cells and
   spectrum picture carry over; CARRIER and card C's ROOT have no meaning here.
5. **The effects library's racks have more parameters than Modular's effect nodes expose**
   (COMPRESSOR: 28 against 7). The nodes wrap the same `fx::` processors, so the missing
   settings exist in the DSP: full parity is a matter of exposing them as node parameters
   (Analog's table `fxparams::specsOf` is the single source; it is JUCE-typed today).
6. **One labelling defect found on the way:** Modular's ECHO listed its note values as
   "1/32, 1/16, 1/16D, 1/8T…" while passing the index to `EchoDelay`, whose own order is
   "1/16, 1/8T, 1/16D, 1/8…" (`shaping::echoDivisionName`). The names now come from the echo
   itself. Stored values and sound are unchanged.

## 3. The bridge: `RackHost`

One per open rack. It implements `osp::plugin::InstrumentHost` for exactly one node:

| The rack asks | `RackHost` answers |
|---|---|
| `apvts()` | a private parameter state with the rack's schema; never the plugin's host parameters |
| a parameter by its ANDOR ID | a `RackParam` binding: rack ID ↔ this node's parameter, with the unit conversion (percent ↔ 0..1, …). A change in the rack is `Session::setParam` on this node (one undo step per action); a change of the node is pulled back |
| `hostTempo()`, `hostSampleRate()` | the engine's (`Engine::hostBpm`, new: the last block's tempo) |
| `outputPeak()` | this node's peak in the running graph |
| `rackFaceplate (kind)` | Analog's faceplate for that device (`faceplateFromIdentity`) |
| layers, instrument presets, the modulation matrix, the arpeggiator | "none" (the racks used here never ask) |

A rack is opened, swapped and closed without touching the audio graph; turning its controls is
a live parameter write (tested: no graph swap).

## 4. Extraction map

| Module | Rack shown | Code | State |
|---|---|---|---|
| `fx.echo` | ANDOR's ECHO (`EchoRack`), Analog's ECHO faceplate, tape deck | shared, unmodified | **done** (Gate B) |
| `fx.space` | `SpaceRack` | shared | Gate C |
| `fx.movement` | `MovementRack` | shared | Gate C |
| `proc.filter` | FILTER: Analog's identity and response maths, cells for this node's parameters | parts + `filtergraph` | Gate C; needs `filtergraph::responseDb` reachable without the engine |
| `proc.morph`, `proc.morph.global` | MORPH: Analog's identity, cells and spectrum picture without CARRIER / card C | parts | Gate C |
| `proc.drive` | Analog's DRIVE page (its parameters are the instrument's own `drive*`) | `FxRack.cpp` once the seam exists, else parts + `DriveProcessor::transfer` | Gate C |
| `fx.compressor`, `fx.phaser`, `fx.flanger`, `fx.shifter`, `fx.filter`, `fx.spread`, `fx.retro`, `fx.convolution` | Analog's effect racks | `FxRack.cpp` | Gate C; needs the upstream seam and the nodes' full settings |
| sources, envelopes, LFO, arpeggiator / sequencer, mixer, satellite, utilities | a specific rack built from the family's parts (archetype per family) | parts | Gate C |

**Upstream seam proposed (Analog, additive, on a branch; pin moved only after Analog's own
suites pass):**
- `FxRack.cpp` / `AnalogRacks.cpp`: replace the two `dynamic_cast` chains by a telemetry
  provider interface (`fxProcessor (id)`, `convolutionImpulse (slot)`, `filterTap`,
  `cutoffNoteHigh`) that `AnalogEngine` implements and registers; move `openFxRack
  (ShellEditor&)` to a shell-only file; a setter for the shown instance.
- `FxParameters.cpp`: the specification table in JUCE-free form under `dsp/fx`, so a kernel
  can expose the same settings.

## 5. Exceptions (recorded as they are made)

| What | Why | Source reused |
|---|---|---|
| The amount of ECHO (Analog's ECHO *macro*) is not a control of the rack | In Analog it is the macro knob above the rack. In Modular it is the module's MIX, on its tile; the rack's LED and repeats graph read it | — |
| ECHO's preset browser reads `Documents/ANDOR/Echo Presets` | The family's own presets and folder, shared with OSP and Analog on the same machine | `RackPresetModel` |
