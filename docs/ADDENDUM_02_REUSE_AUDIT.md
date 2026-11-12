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

## 4. Extraction map (as built)

"shared": the very class ANDOR OSP and Analog show, unmodified, on `RackHost`. "parts": a
rack built in Modular from the family's rack parts (`ParameterRack`, `rackui` cells, the
hardware faceplate) in the device's own identity, with a picture specific to the device
(product/Source/ModuleRack.cpp).

| Module | Rack | Code | Its picture |
|---|---|---|---|
| `fx.echo` | ANDOR's ECHO (`EchoRack`), Analog's ECHO faceplate, tape deck, presets | **shared** | the rack's own repeats graph (parameters and tempo); reels turn while the module sounds |
| `fx.space` | ANDOR's SPACE (`SpaceRack`), all four engines, presets | **shared** | the rack's own (decay portrait, LOW / HIGH CUT handles) |
| `fx.movement` | ANDOR's MOVEMENT (`MovementRack`), five modes, pattern editor, presets | **shared** | the rack's own; SHAPER's playhead from the module's telemetry |
| `proc.filter` | FILTER | parts, Analog's voice-filter identity | small-signal response: Analog's linearisation of its ladder and OTA models (ported, see 5), the module's SVF for the 12 dB modes |
| `proc.morph`, `proc.morph.global` | MORPH | parts, Analog's MORPH identity | the two sounds band by band, live (the module's telemetry) |
| `proc.drive` | DRIVE | parts, Analog's DRIVE identity | `osp::DriveProcessor::transfer` (the same function Analog's graph draws) |
| `fx.compressor` | COMPRESSOR | parts, Analog's identity | `Compressor::cleanCurveDb`; gain reduction and output, live |
| `fx.filter` | FX FILTER | parts | `fx::FxFilter::responseDb` (as Analog's graph) |
| `fx.phaser` / `fx.flanger` / `fx.spread` / `fx.retro` | their devices | parts | the processors' own meters, now telemetry: notches, delay now, correlation and side level, bits / rate / wow |
| `fx.shifter`, `fx.chorus`, `fx.convolution`, `proc.tone` | their devices | parts | output level on a scale (no picture of their own yet) |
| `ctl.env`, `proc.vca` | ENVELOPE / AMP ENVELOPE | parts, modulation accent | the ADSR to scale and its level now |
| `ctl.lfo` | LFO | parts | its shape and its value now |
| `event.stepseq` | STEP SEQUENCER | parts, arpeggiator amber | its sixteen steps, edited on the glass |
| `event.arp`, `event.generative`, `event.clock` | their devices | parts | the notes or steps as they play |
| sources (`osc.analog`, `src.*`) | SOURCE | parts, the layer's colour | the front panel's display (wave, recording and playhead, grains, status); TYPE, recording and plugin on the rail and selector row |
| `spatial.satellite` | SATELLITE | parts, Analog's SPREAD identity | its pan position now |
| `util.mixer` | MIXER (LEVELS / PAN / MUTE-SOLO) | parts, UTILITY identity | channels as set, output now |
| other utilities and controllers | their devices | parts | output level or control value now |
| a group | the plain rack (rename its controls, save as module) | Modular | — |

**Not used:** Analog's `createFilterRack`, `createMorphRack`, `createFxRack`. They cannot be
linked or called without Analog's shell and engine (section 2), and FILTER's is Analog's whole
voice. The seam that would let the effect racks be shared was not built in this pass:

**Upstream seam proposed (Analog, additive, on a branch; pin moved only after Analog's own
suites pass):**
- `FxRack.cpp` / `AnalogRacks.cpp`: replace the two `dynamic_cast` chains by a telemetry
  provider interface (`fxProcessor (id)`, `convolutionImpulse (slot)`, `filterTap`,
  `cutoffNoteHigh`) that `AnalogEngine` implements and registers; move `openFxRack
  (ShellEditor&)` to a shell-only file; a setter for the shown instance.
- `FxParameters.cpp`: the specification table in JUCE-free form under `dsp/fx`, so Modular's
  effect nodes can expose the same settings (COMPRESSOR's KNEE, MULTIBAND, FET's own
  controls, RETRO's machine pages...). With both, `fx.*` nodes could open Analog's own effect
  racks with their pages, presets and meters.

## 5. Exceptions and deviations

| What | Why | Source reused |
|---|---|---|
| The amount of ECHO and SPACE (Analog's macros) is not a control of their racks | In Analog it is the macro knob above the rack. In Modular it is the module's MIX on its tile; the racks' LED and graphs read it | — |
| ECHO / SPACE / MOVEMENT presets live in `Documents/ANDOR/{Echo,Space,Movement} Presets` | The family's own presets and folders, shared with OSP and Analog on the same machine | `RackPresetModel` |
| MOVEMENT's DRIFT / TAPE / CHORUS / PULSE controls share three values (A, B, C) | The module has one set that the chosen mode reads; the rack names them per mode | — |
| FILTER's response formula is ported (40 lines), not linked | `filtergraph::responseDb` is defined in `AnalogRacks.cpp`, which cannot be linked (section 2) | Analog `AnalogRacks.cpp:29-68`, cited in `ModuleRack.cpp` |
| FILTER has no VOICE / PAIR pages, MORPH no CARRIER / card C root | They are Analog's synth, not this module (findings 3, 4) | identity, cells |
| Effect racks show the module's parameters, fewer than Analog's | Finding 5; the proposed seam closes it | identity, cells, the processors' own curves and meters |
| No preset browser on parts racks | Analog's effect presets are tied to its parameter IDs | — |

Paired pictures (Analog's reference above, Modular below): `docs/screenshots/parity/`.
