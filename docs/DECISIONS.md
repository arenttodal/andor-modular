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

## D-21 Held notes keep their pitch when a note module's settings move
KEYBOARD's OCTAVE (and KEY SPLIT, SCALE / TRANSPOSE) used to rebuild the graph and transpose
note-offs with the settings of their own moment: clicks, restarted notes, notes that never
ended. **Decision:** note modules are read live, and each remembers what every note identity
became (`NoteMemory`); a note-off ends exactly that. A held key keeps sounding where it is;
the new setting is for the next notes. Retuning held notes would be a different behaviour
(a pitch control, not a keyboard setting) and is not offered.

## D-22 Mute and Solo are mix state, not bypass
`muted` and `solo` on modules and groups, saved only when set (schema 1 files unchanged),
applied at a source's own output as a 12 ms ramp in the running graph. Mute wins over solo.
`enabled` stays what it was: the module's bypass. A soloed patch says so in the header.

## D-23 One filter for several sources: per voice when they share notes, once when they do not
A processor's audio input sums what is connected. Sources played by the same note stream share
its voices (one filter per voice). Chains with their own note modules cannot (every note of
either would play both), so the processor moves after the voice mix and runs once, each chain
through its own VOICE MIX. The edit says which it did. A module that only exists per voice
(MORPH) refuses instead of guessing.

## D-24 Quick Add is a menu, not a catalogue
53 modules in one scrolling list was a catalogue to read. **Decision:** a compact menu at the
pointer: search, three recent, six categories with submenus. The registry stays the only
catalogue (`NodeDesc::category`; effects, `fx.*`, listed together).

## D-25 The rack replaces the detail column
A permanent 332 px column took a quarter of the board from the patch to show one module.
**Decision:** a module opens as a drawer across the board's foot, one at a time, on demand;
the board keeps its width, its zoom and its place. A group's double-click still enters it.

## D-26 Layout is a rule, applied everywhere, inside the edit
`edit::resolveOverlaps` (pure, deterministic, idempotent): what an edit placed stays; others
make room by the least movement. It runs inside every transaction (one undo) and once on load.
The glide that shows it is the picture only.

## D-27 Escape, scrolling and F
One Escape closes one thing, decided in one place. Scrolling pans with the gesture as the
system delivers it (the direction is the system's setting; a preference reverses it). F is a
letter: a single key that silently refits the view was pressed by accident; FIT is a button.

## D-28 Ebonized ash and black anodized aluminium
Modular's own trim (docs/MATERIALS.md), generated procedurally over the family's housing
render, in the family's skin-pack format. Only the housing piece changes: the ceramic, the
board, the modules and the interaction colours do not. This is the one asset Modular owns
(CLAUDE.md rule 9).

## D-29 A family rack is bound to one module through a host of its own
The family's racks read and write an `osp::plugin::InstrumentHost`: in OSP and Analog, the
instrument. A patch has any number of echoes, so each open rack gets a `RackHost`: that
interface for exactly one node (a private parameter state under the rack's IDs, each bound to
one of the node's parameters; tempo and telemetry from the running graph). Rejected: a fake
`AnalogEngine`, casting Modular's processor to the shell, or pointing racks at plugin-wide
parameters (two echoes would share one).

## D-30 Shared racks where the family has them; siblings from its parts where it cannot share
ECHO, SPACE and MOVEMENT are the shared classes, unmodified. Analog's FILTER, MORPH and
effect racks cast to its shell and engine and cannot be linked into another product; its
FILTER rack is its whole voice. For those, and for modules the family has no rack for, a
device is built from the same parts in the module's identity with a picture of what it does.
The seam that would let the effect racks be shared is specified, upstream, in the audit; it
was not taken on in the same pass as it changes Analog and needs its suites.

## D-31 Modules are completed rather than racks pruned
Where a shared rack offers something the module lacked (SPACE's VINTAGE and BLOOM, MOVEMENT's
SHAPER, ECHO's ASCEND settings), the module gained it from the same shared processors at
their defaults, instead of hiding or disabling parts of the rack. Rule 6 (no fake controls)
and rule 8 (wrap, do not fork).

## D-32 One SOURCE over the existing source modules (stage 1)
The musician's "Source" is a presentation: its TYPE names which existing module it is. No
wrapper node, no renamed type id, no migration: old patches are untouched. `changeSourceType`
changes the node's type in place and keeps each other type on the node ("modes").

## D-33 What a type change does with cables
A cable belongs to the source, not to a type: it stays if the new type can honour it; else it
is set aside with the type it was for and taken back by the first type that can use it. A type
that plays once for all notes joins where its chain's voices meet rather than being refused,
and the edit says so. Only when no such place exists, or a limit would break, is the change
refused. Where a type runs is the musician's choice only if the type left offered one.

## D-34 The seam for Analog's effect racks is built in Analog, on the pinned line
ANDOR OSP's newer default line already shares the FX library (`shared/andor-ui/FxRack`,
`host.fxProcessor`), but neither Modular's OSP pin nor any Analog commit is on that line
yet. The seam was therefore built on the commits Modular pins (Analog `1c0cb6d`, OSP
`e7a3834`), in the same shape: the rack reads its instrument through `FxRackSource`
(processor, impulse, memory, title) and `InstrumentHost` only; `openFxRack (ShellEditor&)`
and the engine lookup moved to a shell-only file. When Analog moves to OSP's shared FX,
Modular's binding carries over (`fxProcessor` becomes the host's own virtual).

## D-35 The effect modules carry the library's whole table; old parameters keep their place
One definition of the settings (Analog's `dsp/fx/FxParameterTable`, JUCE-free). A module
keeps the parameters it had, written as they always were, and gains the rest as detail
parameters (edited in the rack, off the tile, left alone by EVOLVE). Rejected: a second,
hand-kept list in Modular; renaming old ids to the library's; narrowing the rack's knobs to
the old ranges (presets would clamp, negative phaser feedback and MULTIBAND would be missing).

## D-36 Schema 2 with a migration, rather than ranges changed in place
Mappings and modulation depths are saved normalised, so widening a range silently would move
what a macro reaches. The schema went to 2 and `migrateV1` moves them; values are in their own
units and untouched. A setting left at its default is not written, so the processor plays its
built-in value and schema 1 patches render bit for bit (the table's 12 % is `0.01f * 12`, one
float from the built-in `0.12f`).

## D-37 MIX is a knob of the ECHO and SPACE racks through an opt-in of the shared rack
`InstrumentHost::rackAmountParameter` (OSP, default none: ANDOR OSP and Analog keep the macro
above the rack and are pixel-identical). The column grows by what the extra knob needs in the
two densest modes. Rejected: an overlay drawn by Modular over the family's rack.

## D-38 Presets of the racks are ANDOR Modular's own
`<user data>/ANDOR/Modular/Rack Presets/` (`Echo Presets`, `Space Presets`, `Movement
Presets`, `FX Presets/<TYPE>`), set once through `setRackPresetFolder` (OSP) and
`fxpresets::setUserRoot` (Analog). Factory presets ship in the family's code and are the same
everywhere. SHAPER's drawn patterns stay in the family's pattern folder (they are patterns,
shared on purpose, not rack presets).

## D-39 FILTER and MORPH stay Modular's racks and gain a live trace
Analog's FILTER rack is its whole voice and its MORPH graph is a concept view of oscillators
and card C; neither is this module. FILTER now reports the newest voice's cutoff, the cutoff
before the envelope, the envelope's top and the resonance as modulated; its rack draws the
setting quiet behind what plays (as Analog's live FILTER page). MORPH draws the two sounds as
measured and, between them, what the method makes of them (Analog's carrier / card C /
result, from what sounds). DRIVE keeps its sibling rack: Analog's has no OUTPUT.

## D-40 EQUALIZER and UTILITY are modules made of the table alone
Analog's two remaining library effects became `fx.eq` and `fx.utility` with no parameter
list of their own: every parameter is the table's, under its name; a few are named for the
tile (LOW / LOW MID / HIGH MID / HIGH; GAIN / BALANCE / WIDTH). Their racks are Analog's.
TONE EQ (`proc.tone`, per voice or global, three knobs and a tilt) stays: it is a different,
smaller thing and old patches use it.

