# Decisions (ADRs)

Each entry: context, decision, consequences. Reversible unless stated.

## D-01 Pins: OSP at Analog's own pin
OSP's default branch is 38 commits ahead of the OSP revision ANDOR Analog is tested with.
**Decision:** `external/osp` = `e7a3834` (Analog's pin), `external/analog` = Analog's default
`1c0cb6d`; submodules non-recursive (one OSP). **Consequence:** Analog's DSP compiles and
behaves exactly as tested. Moving to a newer OSP means moving both pins together.

## D-02 Compile Analog's reusable DSP from the pin, not its synth
**Decision:** `modular_analog_dsp` lists the reusable files (oscillator models, filters,
LiveMorph, live granulator, chorus, FX library); `Synth.cpp`, `FactoryPatches.cpp` and the
plugin are not built. **Consequence:** no source copies, no edits upstream; Analog's `Patch.h`
enums are a compile-time dependency.

## D-03 Product identity checked, not registered upstream
OSP's `andor_add_instrument` requires an entry in OSP's product registry (an OSP edit) and
brings the three-card shell. **Decision:** Modular uses `juce_add_plugin` directly with its own
identity (ANDOR Modular, `modular`, `Amdr`, `com.osp.andormodular`, manufacturer `Ospx`) and
checks at configure time that no ANDOR product in the pinned registry or Analog's product.json
uses any of it. **Follow-up:** add Modular to OSP's registry in an OSP PR (owner's call).

## D-04 Lanes: voice components with their own pools
**Decision:** voice-domain modules connected to each other form a lane with its own voice pool
and note stream. **Why:** independent arpeggiators per oscillator (spec Demo 4) need separate
note identities; a single global pool would play every oscillator for every note.
**Cost:** total voices = lanes × polyphony; budgets and the cost model account for it.

## D-05 Boundaries accumulate
VOICE MIX and POLY > GLOBAL are global modules with voice-side inputs; every voice of every
feeding lane adds into them. No implicit voice→global conversion exists anywhere else.

## D-06 Global control into a voice is an implicit broadcast
The spec lists an explicit Broadcast node; every global control reaching a voice is already
unambiguous (all voices hear it). **Decision:** allowed directly, marked "every voice hears it"
when the cable is made. Voice→global still requires POLY > GLOBAL.

## D-07 Fixed 32-sample engine grid, 32 samples of latency
**Decision:** the engine renders on an absolute 32-sample grid and delays its output by one
grid block. **Consequence:** bit-identical output at any host block size (tested at 1, 37, 64,
256, 512, 1000); 0.67 ms latency at 48 kHz, reported to the host.

## D-08 Graph swaps crossfade and replay held keys
**Decision:** a new graph fades in over 384 samples (equal power) while the old one fades out;
keys held on the keyboard are played again into the new graph at its first sample.
**Trade-off:** arpeggiator phase and envelope stage restart in the new graph (a clean,
click-free restart rather than state transfer between different topologies).

## D-09 GLOBAL MORPH is new DSP
OSP @ `e7a3834` has no bus-level spectral imprint (it lives on an unmerged Analog branch).
**Decision:** a new STFT module (2048-point Hann, hop 512, A and B in one complex transform,
IMPRINT and FUSE, exact ends via the same endpoint law as LiveMorph, transforms staged across
the hop; 2560 samples latency, aligned with parallel paths). An absent B (plugin missing) hands
A through. WEAVE and pitch glide are per-voice only and are not offered on the global module.

## D-10 SATELLITE re-implemented, level-based
The validated Satellite is on an unmerged branch. **Decision:** PAN / ORBIT / BOUNCE as a
global stereo module using equal-power panning plus a quieter, darker "behind" for ORBIT — no
inter-channel delay, so the mono downmix stays coherent (tested). Per-voice motion = an LFO per
voice into VOICE MIX's PAN input.

## D-11 Parameter modulation at chunk rate; audio-rate only on explicit inputs
Modulation of parameters is resolved once per chunk (≤ 32 samples, ≥ 1.5 kHz at 48 kHz) in
normalised space; FM, SYNC and VCA CV are per-sample inputs.

## D-12 Macros map to normalised ranges; scenes write bases
A macro mapping sets its target to `min + (max − min) · curve(macro)` (later mappings to the
same parameter add their span). Scenes interpolate in normalised space (geometric for Hz),
switch choices at the midpoint, and write the parameters' bases when the scene position moves —
so a knob turned afterwards keeps its value until the scene fader moves again.

## D-13 Host automation: a fixed bank
Eight macros, the scene position and 32 assignable slots bound to (node id, parameter id) in the
patch. Ids never change when modules move; duplicating or deleting modules updates bindings in
the patch, never the host's ids.

## D-14 Compile on the message thread
Measured 0.1–6 ms per factory patch. Revisit if a patch type exceeds ~10 ms (convolution
impulses are the likeliest).

## D-15 Factory recordings are synthesised by ANDOR
No third-party library audio: `tools/make-samples` synthesises a bowed C3, a clockwork loop at
120 BPM and a rain texture deterministically.
