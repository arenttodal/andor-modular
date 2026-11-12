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

## D-16 The interface is the ANDOR interface, composed, not copied
The first prototype had its own dark look. **Decision (2026-11-12, owner request):** Modular is
drawn only with the family's design system, in ANDOR Analog's walnut finish. `product/` compiles
the shared sources it needs from the pinned submodules (`Design`, `OspLookAndFeel`,
`CeramicSkin`, `SpriteKnobRenderer`, `RackFaceplate`, Analog's `FxIdentity`), links `AndorUiData`
(fonts, ceramic skin, ivory knob sprites) and embeds Analog's `walnut-skin.zip` straight from
the submodule. Nothing is copied into this repository and nothing in the submodules is edited.
Parts whose shared implementation is tied to OSP's parameters (`InstrumentFrame.cpp`,
`MainSections.cpp`: the keyboard, the wheel, VOLUME, the preset bar) are re-bound in
`product/Source/Frame.*` with the same pieces, tokens and drawing recipe, each citing its source.
*Consequence:* a move of those recipes upstream is not followed automatically; a later OSP PR
could split them from OSP's bindings so Modular links them directly (listed under NEXT).

## D-17 The instrument's shape: 1448 × 1086, one piece; the board grows in PATCH
The window is the family's canvas scaled uniformly (869–1810 px wide). PLAY is the family's
layout (band, macro panel, keyboard row). In PATCH the board takes the band *and* the macro
panel's room (771 instead of 525 reference px): a patch needs it, and the macros are a
performance surface. Mapping a macro from PATCH is confirmed in the status line.

## D-18 Module faces come from the family's vocabulary
`ui::lookFor` maps each module type to an existing face: Analog's device enamels and motifs for
processors and effects, a source card with its layer badge for sources, the modulation panel's
cream for modulators, the arpeggiator's key and amber for note modules, the routing board's
terminal for OUTPUT and its fanned stack for groups. No module has a face invented here; a type
without its own device wears UTILITY's.

## D-19 The keyboard, the wheel and VOLUME are real
The interface's keys and PITCH wheel play the patch through a lock-free single-writer queue
drained at the start of the next block (`ModularProcessor::playFromInterface`); held host notes
are mirrored to the keys from atomics. VOLUME is the patch's `masterDb`, heard while it moves
(`Engine::setMasterDb`, glided over 10 ms; a level that never moves is applied exactly as
before, so renders are unchanged). There is no MOD wheel: nothing would listen to it (rule 6).

## D-20 PLAY is Analog's front panel, not a title page
The first ANDOR-styled PLAY showed the instrument's name as a headline, its description and an
output scope. **Decision (2026-11-12, owner):** none of that. PLAY is as immediate and
structured as ANDOR Analog's front panel: a source card for every sound source across the band
(badge, mute, display with its modes, knobs), the macro panel under it. The cards expose the
modules' own parameters, so a knob turned on a card is the knob on its tile. One to four
sources sit side by side; five or six go in two rows of three compact cards (Analog has at
most three; Modular's budget is six). What is not a source is reached through the macros, or
one key away in PATCH.

