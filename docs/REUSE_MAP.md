# Reuse map

Where each Modular module's DSP comes from (pins: OSP `e7a3834`, Analog `1c0cb6d`). Details and
risks: [00_DISCOVERY_AUDIT.md](00_DISCOVERY_AUDIT.md).

| Module | Comes from | New in Modular |
|---|---|---|
| ANALOG OSC | Analog `Oscillator.h`, `PolyBlep.h` (CLASSIC/DCO/WIDE, SUPER SAW) | drift, FM/SYNC inputs, keytrack, WIDE edge filter coefficient |
| ORGANIC SAMPLE | Analog `OrganicVoice.h` + OSP analysis / InstrumentBuilder | asset library staging |
| GRANULAR | OSP `GranularSource` | voice/global wrapper, grain telemetry |
| NOISE | — | new (white/pink/brown, tone, motion) |
| RHYTHMIC SOURCE | OSP `PlaybackSource` read (Analog's Hermite read) | beat-slice host sync, follower, onset trigger, tempo analysis |
| EXTERNAL INSTRUMENT | design after Analog's unmerged Kontakt branch | `ExternalInstrument` interface, JUCE host, surrogate tests |
| LIVE INPUT | — | new (with feedback guard) |
| FILTER | Analog `LadderFilter.h`, `OtaFilter.h` | TPT SVF modes, keytrack, env input |
| AMP ENVELOPE / ENVELOPE | Analog `Envelope.h` | gates (key / input / global notes) |
| MORPH | Analog `morph/LiveMorph` | wrapper (pitch per side, layered output) |
| GLOBAL MORPH | — | new STFT imprint/fuse |
| DRIVE, SPACE (rooms, ABYSS), ECHO, MOVEMENT | OSP `DriveProcessor`, `SpaceReverb`, `AbyssReverb`, `EchoDelay`, `MovementBus` | wrappers |
| ENSEMBLE CHORUS | Analog `JunoChorus` | wrapper |
| PHASER, FLANGER, FREQUENCY SHIFTER, COMPRESSOR, FX FILTER, RETRO, STEREO SPREAD, CONVOLUTION | Analog `fx/*` (`makeProcessor`) | wrappers; convolution impulse chosen at compile |
| ARPEGGIATOR | OSP `Arpeggiator` | one per node, merged note ids |
| STEP SEQUENCER, GENERATIVE, CLOCK, NOTE MERGE, KEY SPLIT, SCALE/TRANSPOSE | — | new |
| LFO, RANDOM/S&H, FOLLOWER, NOTE INFO, MATH, SLEW | — | new (seeded with `osp::Prng`) |
| SATELLITE, PAN/WIDTH | — | new (level-based) |
| VOICE MIX, POLY>GLOBAL, MIXER, SWITCH, SPLIT/MERGE, GAIN/VCA, FEEDBACK DELAY, OUTPUT, SCOPE | — | new |
| Engine hand-over | pattern of OSP `ModelExchange` | atomic publish + lock-free retire ring |
| Fonts | OSP `shared/andor-ui/Resources/fonts` | — |
| Interface: tokens, materials, drawing, look-and-feel, ceramic skin, sprite knobs | OSP `shared/andor-ui/Source` (`Design`, `OspLookAndFeel`, `CeramicSkin`, `SpriteKnobRenderer`, `RackFaceplate`) + `AndorUiData` | compiled into `product/` from the submodule, unchanged |
| Interface: housing | OSP's housing render (the ceramic body, the cheeks' shape and light) | the cheeks' material, the end caps, rails and fasteners: Modular's own pack (`tools/make_ebonized_ash_skin.py`, docs/MATERIALS.md); the interaction colours as Analog's finish set them (`ui::applyMaterials`) |
| Interface: device enamels and motifs | Analog `product/Source/FxIdentity.*` | compiled from the submodule, unchanged; mapped to modules in `ui::lookFor` |
| Interface: routing board, keyboard, wheel, VOLUME, preset bar, macro setup | Analog `FxRouting.cpp`; OSP `InstrumentFrame.cpp`, `MainSections.cpp` | their drawing recipe re-bound to the patch in `product/Source/{Canvas,Frame,PlayView}.*` (docs/DECISIONS.md D-16) |
