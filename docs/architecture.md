# Architecture

```
        message thread                                   audio thread
 ┌────────────────────────────────┐              ┌──────────────────────────────────┐
 │ Document (dsp/graph/Document)  │              │ Engine::process (any block size) │
 │  nodes, cables, groups, macros │              │  MIDI queued at sample time      │
 │  scenes, bindings, view        │              │  fixed 32-sample grid blocks:    │
 │        │ edit::* (one undoable │              │   controls (macros, scenes,      │
 │        │  transaction each)    │              │   host slots) -> event nodes ->  │
 │        ▼                       │   atomic     │   chunks split at every event:   │
 │ Validator -> Compiler ─────────┼──pointer────►│    lanes (voices) and global     │
 │  (kinds, domains, fan-in,      │   publish    │    units in topological order,   │
 │   cycles, budgets)             │              │    boundaries, feedback writes   │
 │        ▲            ◄──────────┼──lock-free───┤  swap: 384-sample equal-power    │
 │ AssetLibrary (worker thread):  │   retire     │   crossfade, held keys replayed  │
 │  decode -> OSP analysis ->     │   ring       │  master gain + safety limiter    │
 │  provisional -> continuation   │              │  output delayed by 32 samples    │
 └────────────────────────────────┘              └──────────────────────────────────┘
```

## Kernel (`dsp/`, pure C++20)

- **Types** (`graph/Types.h`): `PortKind` (notes, pitch, gate, trigger, control, audioMono,
  audioStereo, clock), `Domain` (voice, global, event), `ParamCurve`. Mono audio ports are mono
  per voice and stereo when the instance is global (`effectiveKind`).
- **Descriptors** (`graph/Node.h`): `NodeDesc` — stable type id, version, ports, parameters
  (range, curve, unit, Quick, modulatable, structural, progressive disclosure), role (source,
  output, boundary, feedback, event), cost, latency, factory functions. `Registry::builtIn()`
  holds the 53 built-in types (`dsp/nodes/*`).
- **Validator** (`graph/Validator.*`): one rule set for the canvas (`checkConnection`, before a
  cable exists) and the compiler (`validate`, every saved cable): kinds, domains, fan-in,
  cycles (only `util.feedback` closes a loop), budgets, missing modules, group depth.
- **Compiler** (`graph/Compiler.*` → `CompiledGraph.h`):
  - **Lanes**: connected components of voice-domain modules. Each lane has its own voice pool
    (polyphony) and its own merged note stream (from the event modules feeding any of its
    notes ports). Two oscillators with two arpeggiators are two lanes — independent notes; a
    per-voice MORPH between them joins them into one lane (they then play the same note).
  - **Boundaries** (VOICE MIX, POLY > GLOBAL) are global modules whose voice-side inputs every
    lane voice *accumulates* into, then run once.
  - **Units**: lanes and global modules sorted topologically (feedback inputs excluded); event
    modules sorted separately and run first.
  - **Buffers**: every output port of every instance owns a 32-sample stereo buffer; inputs
    point at them (multi inputs get a sum buffer; global inputs from shorter parallel paths get
    an alignment delay so latent branches recombine without combing).
  - **Controls**: base values (atomics the message thread and host bindings write), eight
    macro channels and the groups' control channels (a macro can drive a group control which
    drives its modules' parameters), scene targets, host bank bindings.
- **Engine** (`graph/Engine.*`): see the diagram. Voice allocation per lane (poly with
  64-sample steal fades preferring released voices, mono and legato with a held-note stack and
  glide), note ids so one stream's note-off never ends another's note, sustain pedal, pitch
  bend, a runtime voice cap for CPU admission. Telemetry is written to atomics for the UI.
- **Edits** (`dsp/edit/*`): smart wiring, undo history, groups and modules, scenes,
  auto-arrange, EVOLVE, the factory builder.
- **I/O** (`dsp/io/*`): WAV, the asset library, the offline renderer.

## Product (`product/`, JUCE)

`ModularProcessor` (fixed parameter bank, state) → `Session` (document + history + assets +
external host; compile on every edit, ~0.1–6 ms) → `Editor` (PATCH: `Canvas` + `NodeView`s,
`QuickAdd`, `DetailPanel`; PLAY: `PlayView`, `MacroKnob`s, `ScenePanel`; `Frame` for the family's header and keyboard row). The look is the family's own (docs/UI_COMPONENTS.md).

## Why these choices (details in DECISIONS.md)

- Compile on the message thread: measured 0.1–6 ms for every factory patch; a background
  compile thread would add hand-over complexity for no audible gain at these sizes.
- Per-port buffers instead of lifetime-shared scratch: 32 nodes × 16 voices × 2 × 32 floats is
  ~128 kB; reuse would save memory nobody needs and cost clarity.
- A fixed 32-sample grid with 32 samples of latency: output does not depend on the host's block
  size (offline bounce = live), control-rate decisions fall on the same samples every time.
