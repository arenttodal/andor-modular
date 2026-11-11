# Port types and domains

| Kind | Glyph (shape + muted colour) | Carries | Units | Rate |
|---|---|---|---|---|
| NOTES | triangle, clay | note on/off with id, velocity 0..1, channel, sample offset | MIDI | event (per block) |
| PITCH | diamond with a bar, lilac | pitch offset | 1.0 = one octave | control |
| GATE | square, slate | held level | 0 / 1 | control |
| TRIGGER | square, slate | one-sample pulses | 0 / 1 | control |
| CLOCK | square, slate | pulses at a musical division | 0 / 1 | control |
| CONTROL | diamond, spore | modulation | unipolar 0..1 or bipolar −1..1 (declared per port) | control (per sample buffers; parameters read once per chunk) |
| AUDIO | circle, bone | audio; mono per voice, stereo when the module is global | linear | audio |
| STEREO | circle with a ring, bone | stereo audio | linear | audio |

## Domains

- **VOICE** — runs once per sounding note of its lane. Sources, AMP ENVELOPE, per-voice FILTER /
  DRIVE / TONE / MORPH, NOTE INFO, per-voice LFO / ENVELOPE / RANDOM / MATH / SLEW / GAIN / SWITCH.
- **GLOBAL** — runs once. Everything after VOICE MIX, the effects, SATELLITE, GLOBAL MORPH,
  EXTERNAL INSTRUMENT, LIVE INPUT, RHYTHMIC SOURCE, global LFOs, the OUTPUT.
- **EVENT** — note streams, once per 32-sample grid block before audio: KEYBOARD, ARP, STEP
  SEQUENCER, GENERATIVE, NOTE MERGE, KEY SPLIT, SCALE / TRANSPOSE, CLOCK (its control outputs
  are global signals).
- Flexible modules (FILTER, DRIVE, TONE, NOISE, GRANULAR, LFO, ENVELOPE, RANDOM, MATH, SLEW,
  GAIN, SWITCH, AMP ENVELOPE) choose VOICE or GLOBAL per instance (node menu).

## Allowed cables

| From → To | Rule |
|---|---|
| notes → notes | always (several cables merge; distinct note ids) |
| audio → audio | same domain; mono into stereo duplicates; stereo into mono is refused (STEREO SPLIT) |
| control-like → control/gate/trigger/pitch input | yes |
| control-like → audio input marked "accepts control" (FM, SYNC, CV) | yes |
| control-like → a parameter | modulation with a depth −100..100 % (several add) |
| audio → control input or parameter | refused (ENVELOPE FOLLOWER suggested) |
| voice → global | only into VOICE MIX (audio) or POLY > GLOBAL (control) |
| global control → voice | yes: every voice hears it |
| global audio → voice | refused |
| a second cable into a single input | refused (MIXER / VOICE MIX / NOTE MERGE / MATH suggested) |
| a loop | refused unless it passes through FEEDBACK DELAY (≥ one 32-sample block, gain < 1) |
| from OUTPUT | refused (it goes to the host) |

The canvas checks before a cable exists and shows the reason and the converter.
