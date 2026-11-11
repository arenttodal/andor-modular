# Factory content

Built by `dsp/edit/Factory.cpp` with the same edit operations a musician uses; written by
`modular-render --write-factory`; checked byte-for-byte by `FactoryTests`. Recordings are
ANDOR's own (`tools/make-samples`: bowed C3, clockwork loop at 120 BPM, rain texture), so the
whole set plays immediately after installation with no external library.

| # | Patch | What it shows (spec demo) | Sources | Macros | Render (8 s, `tools/render-demos.sh`) |
|---|---|---|---|---|---|
| 1 | Basic Poly | the starting point: saw, ladder, filter envelope, hall (Demo 1) | 1 | 8 | peak 0.32, rms 0.077, 42× RT |
| 2 | Analog Organic Morph | DCO + bowed recording fused by MORPH, filtered per voice | 2 | 8 | 0.47 / 0.079, 11× |
| 3 | Clockwork | a pad breathing with a ticking loop at host tempo; envelope → filter and VCA (Demo 3) | 2 | 6 | 0.33 / 0.033, 67× |
| 4 | Three Arps | sustain / eighths / three-octave run, the third in a SATELLITE orbit (Demo 4) | 3 | 4 | 0.73 / 0.143, 42× |
| 5 | Satellite Orchestra | three voices in ORBIT, BOUNCE and PAN around one room | 3 | 3 | 0.53 / 0.101, 30× |
| 6 | Granular Rain | grains of the bowed note per voice, slowly scanned, into ABYSS | 1 | 4 | 0.33 / 0.062, 27× |
| 7 | Generative Sequence | a seeded D-dorian melody for two oscillators an octave apart (Demo 5) | 2 | 4 | 0.55 / 0.077, 75×; re-render identical |
| 8 | Six Oscillator Stack | six oscillators per note (all three models) into OTA + ensemble | 6 | 2 | 0.41 / 0.089, 33× |
| 9 | Bowed Machine | the group instrument with four controls (BOW, RESIN, TONE, RING) on macros (Demo 2, 9) | 2 | 5 | 0.43 / 0.080, 8× |
| 10 | Two-Morph Dream | per-voice MORPH + GLOBAL MORPH with a rain cloud, ABYSS (Demo 6) | 3 | 4 | 0.74 / 0.085, 11× |
| 11 | Scene Performance | STILLNESS → MOTION → FRACTURE → AFTERGLOW on the scene fader (Demo 7) | 1 | 8 | 0.35 / 0.068 (sweep 0→3), 19× |
| 12 | External Instrument Demo | a hosted plugin imprinted onto an analog carrier by GLOBAL MORPH; plays the carrier alone without the plugin (Demo 10) | 2 | 2 | 0.58 / 0.156, 37× (no plugin installed here) |

Three or more patches use no recording at all (1, 4, 5, 7, 8, 11).

## Modules (`presets/modules/*.andormodule`)

| Module | Inside | Controls |
|---|---|---|
| BOWED MACHINE | ANALOG OSC (DCO), ORGANIC SAMPLE, MORPH, FILTER, AMP ENVELOPE | BOW, RESIN, TONE, RING |
| SUB BASS VOICE | DCO with sub, FILTER, AMP ENVELOPE | TONE, SUB, PUNCH |
| PLUCK VOICE | ANALOG OSC, FILTER, ENVELOPE → cutoff, AMP ENVELOPE | BRIGHT, SNAP, LENGTH |
| TAPE SPACE | ECHO → SPACE | ECHO, REPEATS, ROOM |
| ORBIT BUS | SATELLITE → ENSEMBLE CHORUS | ORBIT, SPEED, ENSEMBLE |
| RHYTHM GATE | LFO (synced square) → GAIN / VCA | DEPTH, SHAPE |

Final selection of the twelve is an owner taste decision (spec §12): these are working,
distinct starting points, not a curated release set. Listening review pending.
