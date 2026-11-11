# ANDOR Modular — user guide (prototype)

## Your first sound (60 seconds)
1. **PATCHES → New (empty).**
2. Right-click the canvas (or press **Cmd/Ctrl + K**), type `analog`, press **Return**.
   KEYBOARD → ANALOG OSC → AMP ENVELOPE → VOICE MIX → OUTPUT appears, connected. Play.
3. Select the oscillator, right-click the canvas, type `filter`, Return: a FILTER is inserted
   after it. Turn CUTOFF on its card.
4. Press **PLAY**: right-click a knob in PATCH → *Assign to macro* to put it on the front panel.

## The canvas
- **Add**: right-click empty space; on a **port**, only modules that fit are listed; on a
  **cable**, *Insert a module here*. A module added while another is selected goes after it
  (or asks where, when there are several places).
- **Connect**: drag from a port to a port, onto a module (its first fitting port), or onto a
  knob (a control signal then modulates it). Refused cables say why and what would fit.
- **Drop a source on a source**: LAYER (both sound), MORPH (fused), MODULATE (FM), REPLACE.
- **Drop a recording** (WAV, AIFF, FLAC) on the canvas: ORGANIC SAMPLE, GRANULAR or RHYTHMIC
  SOURCE; on a recording module: relink. Analysis runs in the background; the card shows
  LOADING, then PLAYABLE, ANALYSING, then the waveform.
- **Move** by dragging a card's header; **pan** by scrolling (or Alt / middle-drag); **zoom**
  with Cmd/Ctrl + scroll or a pinch; **F** fits, **0** is 100 %.
- **Select** with a click, Shift-click, or a rubber band; **Delete**; **Cmd+D** duplicate
  (**Shift** with links); **Cmd+G** group; **Tab** next module; **Return** opens its detail.
- The LED on a card bypasses it. The chip says where it runs: VOICE (per note), GLOBAL (once),
  NOTES (note streams). Flexible modules switch in their menu.
- Every action undoes in one step (**Cmd+Z**); the message at the bottom has its own UNDO.

## Notes and voices
- KEYBOARD is the host's MIDI. ARPEGGIATOR, STEP SEQUENCER and GENERATIVE make note streams;
  add an ARP while a source is selected and it goes between the keyboard and that source.
- Each chain of per-note modules has its **own voices and its own notes**: two oscillators
  with two arpeggiators play independently. A per-voice MORPH joins two chains (one note).
- POLY 1–16, MONO (retrigger) and LEGATO (glide while held) in the top bar.
- A notes cable can transpose (right-click it).

## Groups and modules
Select modules → **Cmd+G**. Double-click a group to enter it (breadcrumb at the top left, Esc
to leave). In the detail panel: rename its controls, **Save as module**. **PATCHES → Add a
module** brings back the factory and your saved modules. Groups count their modules against
the limits (6 sources, 32 modules).

## PLAY, macros, scenes, EVOLVE
- Eight macros: right-click one to rename, colour, set its default, and change each mapping's
  range and curve or remove it. Macros are host parameters (automation records).
- Scenes: press **STORE**, then a scene key, to capture every parameter and macro. Click a key
  to recall it; the **scene fader** morphs continuously (modes switch halfway). The fader is a
  host parameter.
- **EVOLVE** (Cmd/Ctrl + E): SUBTLE / MODERATE / WILD, locks for scale, rhythm, routing and
  levels, a seed. **A / B** compares with the original, **KEEP** keeps it, one UNDO goes back.
  The same seed always gives the same variation.

## Host automation
Right-click any knob → *Automate from the host* → Assign 1–32. The host sees fixed parameters
(Macro 1–8, Scene, Assign 01–32) whose ids never change.

## External instruments
Add **EXTERNAL INSTRUMENT**, then *CHOOSE PLUGIN…* in its detail panel (a .vst3, or .component
on macOS). It is global: one instance hears every note. Route it through GLOBAL MORPH to imprint
it onto a synth bus. Its state is saved in your patch; if it is missing later, the module stays
silent and keeps everything until the plugin is back.

## Limits (prototype)
6 sound sources, 32 modules, 64 cables, 6 arpeggiators, 2 MORPHs, 32 modulations, 1 external
instrument. Per-voice MORPH at small buffer sizes: keep to 4 voices at 32 samples, 8 at 64.
