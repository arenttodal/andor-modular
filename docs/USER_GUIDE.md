# ANDOR Modular — user guide (prototype)

## Your first sound (60 seconds)
1. **Click the patch's name in the header → New (empty).**
2. Right-click the board (or press **+ ADD MODULE** / **Cmd/Ctrl + K**): a small menu opens
   at the pointer. Type `analog`, press **Return**.
   KEYBOARD → ANALOG OSC → AMP ENVELOPE → VOICE MIX → OUTPUT appears, connected. Play.
3. Select the oscillator, right-click the canvas, type `filter`, Return: a FILTER is inserted
   after it. Turn CUTOFF on its card.
4. Right-click a knob → *Assign to macro*, then press the **PLAY** key in the header (or
   **Ctrl + Tab**): the front panel. Each sound source has its card (mute, its wave or
   recording, its modes and knobs); under them the eight macros and the scenes. The key at a
   card's top right shows that source in PATCH with every control.

## The menu (Quick Add)
Search at the top, the last three modules you added, then six categories: Sources, Processors,
Controllers, Spatial, Effects, Utilities. Rest the pointer on a category (or press **Right**)
and its modules open beside it. Type at any time for the best matches. **Up / Down** move,
**Right / Left** enter and leave a category, **Return** adds one module, **Escape** goes back
one level. On a port or a cable the menu only offers what fits there.

## The rack
**Double-click a module** (or select it and press **Return**): all its controls open in a
drawer across the foot of the board: what it is on the left, its modes and knobs in the middle
(the step editor for STEP SEQUENCER, its incoming modulation with depths), its live display on
the right. The board keeps its width and its place. Double-click another module to swap; **X**
or **Escape** closes it. Right-click a knob there as on a tile (macros, automation,
modulation). Double-clicking a group opens the group instead.

## Escape
One press closes one thing, the topmost: a drag you have started (a moved module goes back),
the menu's category, the menu, EVOLVE, the rack, the group you are in, then the selection.
It never changes the patch.

## The canvas
- **Add**: right-click empty space; on a **port**, only modules that fit are listed; on a
  **cable**, *Insert a module here*. A module added while another is selected goes after it
  (or asks where, when there are several places).
- **Connect**: drag from a port to a port, onto a module (its first fitting port), or onto a
  knob (a control signal then modulates it). Refused cables say why and what would fit.
- **Several sources, one filter**: drag a second source's OUT onto a filter's or an effect's
  IN. Sources played by the same notes are summed into it, one filter per voice. Sources with
  their own arpeggiators cannot share voices, so the filter is moved after the voice mix and
  runs once for all of them; the status line says which happened. MORPH keeps its A and B.
- **Drop a source on a source**: LAYER (both sound), MORPH (fused), MODULATE (FM), REPLACE.
- **Drop a recording** (WAV, AIFF, FLAC) on the canvas: ORGANIC SAMPLE, GRANULAR or RHYTHMIC
  SOURCE; on a recording module: relink. Analysis runs in the background; the card shows
  LOADING, then PLAYABLE, ANALYSING, then the waveform.
- **Move** by dragging a tile: modules never overlap; where you drop one it stays, and the
  others glide aside (one undo puts everything back). **Pan** by scrolling: the board follows
  the gesture as your system delivers it (MORE → Board scrolling → Traditional reverses it);
  or Alt / middle-drag. **Zoom** with Cmd/Ctrl + scroll or a pinch. **FIT** in the board's
  header shows the whole patch; **0** is 100 %. (F is not a shortcut.)
- **Select** with a click, Shift-click, or a rubber band; **Delete**; **Cmd+D** duplicate
  (**Shift** with links); **Cmd+G** group; **Tab** next module; **Return** opens its rack.
- **M** and **S** on a source (and on a group) mute and solo it, without a click and without
  restarting held notes. S again ends the solo; **Shift + S** adds another; M wins over S;
  **SOLO ×** in the header (only while something is soloed) ends every solo. The same keys are
  on PLAY's cards.
- The light on a tile bypasses it (a short cross-fade). Under its name: its mode (click to change it), or where it
  runs: PER VOICE (each note), GLOBAL (once), NOTES (note streams).
- Every action undoes in one step (**Cmd+Z**); the status line at the bottom says what
  happened, with its own UNDO.
- The keyboard and the PITCH wheel at the bottom play the patch; VOLUME (top right) is its level.

## Recordings
A new ORGANIC SAMPLE, GRANULAR or RHYTHMIC SOURCE starts with one of ANDOR's own recordings so
it sounds at once; drop yours on it. A recording with a clear pitch is played in tune across
the keys. One without (speech, noise, drums) is played as recorded on middle C: its card says
NO CLEAR PITCH. Set ROOT yourself to override either.

## Notes and voices
- Changing KEYBOARD's OCTAVE (or a split, a scale, a transposition) while keys are held leaves
  those notes where they are; they end when you release them. The next notes use the new setting.
- KEYBOARD is the host's MIDI. ARPEGGIATOR, STEP SEQUENCER and GENERATIVE make note streams;
  add an ARP while a source is selected and it goes between the keyboard and that source.
- Each chain of per-note modules has its **own voices and its own notes**: two oscillators
  with two arpeggiators play independently. A per-voice MORPH joins two chains (one note).
- POLY 1–16, MONO (retrigger) and LEGATO (glide while held) on the card to the right of the keyboard.
- A notes cable can transpose (right-click it).

## Groups and modules
Select modules → **Cmd+G**. Double-click a group to enter it (breadcrumb at the top left, Esc
to leave). In its rack (select it, Return): rename its controls, **Save as module**. **The patch's name → Add a
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
Add **EXTERNAL INSTRUMENT**, then *CHOOSE PLUGIN…* in its rack (a .vst3, or .component
on macOS). It is global: one instance hears every note. Route it through GLOBAL MORPH to imprint
it onto a synth bus. Its state is saved in your patch; if it is missing later, the module stays
silent and keeps everything until the plugin is back.

## Limits (prototype)
6 sound sources, 32 modules, 64 cables, 6 arpeggiators, 2 MORPHs, 32 modulations, 1 external
instrument. Per-voice MORPH at small buffer sizes: keep to 4 voices at 32 samples, 8 at 64.
