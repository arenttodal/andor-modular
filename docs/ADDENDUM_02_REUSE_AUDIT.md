# Addendum 02 — reuse audit: the family's racks and source cards

_2026-11-12. Modular `5c4ee89` + this work. Pins read: OSP `e7a3834`, Analog `1c0cb6d`. Sections 1 to 3 describe those commits; **section 4 is as built after the second pass** (the seam of section 4 exists now: Analog `0f5e01b`, OSP `c21fe80`)._

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

## 4. Extraction map (as built, second pass)

"shared": the very class ANDOR OSP and Analog show, on `RackHost`. "parts": a rack built in
Modular from the family's rack parts (`ParameterRack`, `rackui` cells, the hardware
faceplate) in the device's own identity, with a picture specific to the device
(product/Source/ModuleRack.cpp).

| Module | Rack | Code | Its picture |
|---|---|---|---|
| `fx.echo` | ANDOR's ECHO (`EchoRack`), Analog's ECHO faceplate, tape deck, presets, **MIX** | **shared** | the rack's own repeats graph; reels turn while the module sounds |
| `fx.space` | ANDOR's SPACE (`SpaceRack`), all four engines, presets, **MIX** | **shared** | the rack's own (decay portrait, LOW / HIGH CUT handles) |
| `fx.movement` | ANDOR's MOVEMENT (`MovementRack`), five modes, pattern editor, presets | **shared** | the rack's own; SHAPER's playhead from the module's telemetry |
| `fx.compressor`, `fx.phaser`, `fx.flanger`, `fx.shifter`, `fx.filter`, `fx.spread`, `fx.retro`, `fx.convolution`, `fx.eq`, `fx.utility` | ANDOR Analog's effect rack (`FxRack.cpp`, `createFxRackFor`): its pages (MAIN / DETAIL; RETRO's four), modes, preset browser with Analog's factory presets, every control | **shared** | Analog's own graphs, metering the processor that runs in this module (gain reduction, notches, comb, correlation, the impulse) |
| `proc.filter` | FILTER | parts, Analog's voice-filter identity | response at the setting, and **live**: the newest voice's cutoff and resonance, the envelope's travel (the module's telemetry) |
| `proc.morph`, `proc.morph.global` | MORPH | parts, Analog's MORPH identity | the two sounds as measured, and between them **the result** the method makes at this position |
| `proc.drive` | DRIVE | parts, Analog's DRIVE identity | `osp::DriveProcessor::transfer` (the function Analog's graph draws); kept because Analog's rack has no OUTPUT |
| `fx.chorus`, `proc.tone` | their devices | parts | output level on a scale (Analog has no rack for these two) |
| `ctl.env`, `proc.vca` | ENVELOPE / AMP ENVELOPE | parts, modulation accent | the ADSR to scale and its level now |
| `ctl.lfo` | LFO | parts | its shape and its value now |
| `event.stepseq` | STEP SEQUENCER | parts, arpeggiator amber | its sixteen steps, edited on the glass |
| `event.arp`, `event.generative`, `event.clock` | their devices | parts | the notes or steps as they play |
| sources (`osc.analog`, `src.*`) | SOURCE | parts, the layer's colour | the front panel's display; TYPE, recording and plugin on the rail and selector row |
| `spatial.satellite` | SATELLITE | parts, Analog's SPREAD identity | its pan position now |
| `util.mixer` | MIXER (LEVELS / PAN / MUTE-SOLO) | parts, UTILITY identity | channels as set, output now |
| other utilities and controllers | their devices | parts | output level or control value now |
| a group | the plain rack (rename its controls, save as module) | Modular | — |

**The seam (built upstream, each on its repository's `claude/epic-feynman-fsf8ij`, with its
suites):**

- Analog `product/Source/FxRackSource.h`: what the rack reads of its instrument beyond the
  parameters (`fxProcessor`, `fxConvolutionImpulse`, a rack's memory, its title). A host that
  is one needs nothing; ANDOR Analog installs a finder for its engine (`FxRackShell.cpp`, the
  only file of the rack that knows the shell: `openFxRack (ShellEditor&)` lives there too).
  `FxRack.cpp` no longer includes or casts to `AnalogEngine`, `ShellProcessor` or
  `ShellEditor`. `createFxRackFor (host, id)` shows one named effect without the process-wide
  "shown" state.
- Analog `dsp/fx/FxParameterTable.*`: the one table of every effect's settings (names,
  ranges, defaults, how each is written), free of JUCE. `FxParameters.cpp` builds Analog's
  host parameters from it, unchanged (tested name by name).
- Analog `fxpresets::setUserRoot`; OSP `setRackPresetFolder`: an instrument's own preset
  folders.
- OSP `InstrumentHost::rackAmountParameter (kind)`: the opt-in MIX knob of ECHO and SPACE.

**How Modular uses it:** `RackHost` is an `FxRackSource` for its one module (the processor
found in the running graph through `AnalogFxAccess`, `dsp/nodes/AnalogFx.h`); its parameter
state is Analog's FX layout, each ID bound to the module's parameter by the table's name
(`analogFxLinks`); ON is the module's light. Two compressors are two hosts, two processors,
two memories.

**OSP's newer default line** already holds the FX library as shared code
(`shared/andor-ui/Source/FxRack.*`, `host.fxProcessor`), but no Analog commit is on that line
and Modular's OSP pin is not an ancestor of it. The seam above has the same shape, so the
binding carries over when the pins move (D-34).

**Not used:** Analog's `createFilterRack` and `createMorphRack` (FILTER's is Analog's whole
voice, MORPH's speaks of oscillators 1 / 2 and card C: findings 3 and 4), and its DRIVE rack
(no OUTPUT).

## 5. Exceptions and deviations

| What | Why | Source reused |
|---|---|---|
| ECHO and SPACE carry a MIX knob the family's racks do not show | In Analog the amount is the macro knob above the rack; here the rack is the whole device (opt-in of the shared rack, D-37) | `UnifiedRack` |
| Rack presets live in `<user data>/ANDOR/Modular/Rack Presets/` | ANDOR Modular's own (the owner's choice); factory presets are the family's, in code | `RackPresetModel`, `fxpresets` |
| An effect's heading is the module's name, not "COMPRESSOR 1" | A patch has any number of each | `FxRackSource::fxRackTitle` |
| THRESHOLD's knob travel in the COMPRESSOR rack is strongly skewed | It is Analog's own parameter range (its "no centre" marker, -1, lies inside -60..0 dB); the same in Analog. Reported, not changed here. *Since fixed in Analog (pin `9685c81`; STATUS.md, 2026-11-13)* | — |
| MOVEMENT's DRIFT / TAPE / CHORUS / PULSE controls share three values (A, B, C) | The module has one set that the chosen mode reads; the rack names them per mode | — |
| FILTER's response formula is ported (40 lines), not linked | `filtergraph::responseDb` is defined in `AnalogRacks.cpp`, which is Analog's voice rack | Analog `AnalogRacks.cpp:29-68`, cited in `ModuleRack.cpp` |
| FILTER has no VOICE / PAIR pages, MORPH no CARRIER / card C root | They are Analog's synth, not this module (findings 3, 4) | identity, cells |
| No preset browser on parts racks | They are not a family rack; their modules have no presets yet | — |

Paired pictures (Analog's reference above, Modular below): `docs/screenshots/parity/`.
