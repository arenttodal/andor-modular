# DSP contracts

## Processor
`prepare` (allocates; compile thread) → `reset` → per voice `start (VoiceInfo, retrigger)` /
`release` → `process (ChunkContext, ProcessIO)` (≤ 32 samples; no allocation, no locks) →
`active()` (envelopes: voice ends when every `endsVoice` module is idle) → `telemetry`.
Boundaries add `beginChunk` / `accumulate`; FEEDBACK DELAY adds `writeFeedback` (after the
chunk). `latency()` is reported and aligned by the compiler.

## Timing
- Engine grid: 32 samples from the engine's start; MIDI keeps its sample offset and splits
  chunks; output delayed by 32 samples.
- Host time: PPQ from the play head per host block, extrapolated per grid block; free-running
  at the last tempo (120 BPM default) when stopped. Synced LFOs, SATELLITE, CLOCK, sequencers
  and the RHYTHMIC SOURCE read PPQ, so bounces repeat exactly.

## Voices
- Each lane: polyphony 1/2/4/8/16, or MONO / LEGATO (held-note stack, glide).
- Steal order: the oldest released voice, then the oldest sounding one, with a 64-sample fade.
- A chain without an envelope ends 256 samples after its note-off (fade).
- Note ids: a source's events carry ids; merged streams are salted per source, so equal pitches
  from two streams are two voices and never end each other.

## Modulation
`value = denormalise (clamp (base_norm + Σ depth · source[0], 0, 1))` once per chunk, after
macros and scenes. Choice and toggle parameters are not modulatable.

## Macros, group controls, scenes
- Macro (0..1, smoothed per block) → mappings: `min + (max − min) · curve (m)` in normalised
  space; curves linear, inverted, log (fast start), exp (slow start), bipolar (S).
- A macro may drive a group control; a group control drives its modules' parameters.
- Scenes store native values per `node/param` and macro positions; the scene position (0..3,
  host parameter) interpolates captured neighbours in normalised space; choices switch at 0.5.

## Morph
- **MORPH (voice):** ANDOR Analog's LiveMorph, oversampling 1. A = carrier, B = source; both at
  the voice's pitch (plus A/B PITCH offsets). Ends: the sources themselves (with LEVEL MATCH off,
  sample for sample: tested); middle: partial fusion, never a crossfade (tested).
- **GLOBAL MORPH:** STFT 2048 / hop 512, Hann/Hann at 75 % overlap; IMPRINT (A's magnitude ×
  B's smoothed envelope ÷ A's, ≤ +30 dB, A's phase) or FUSE (geometric magnitude, the louder
  side's phase); `out = (1−p)(1−b)·A + b·M + p(1−b)·B`, `b = √(4p(1−p))`, A and B delayed to
  M; latency 2560 samples. B below −66 dBFS hands A through.

## Safety
FEEDBACK DELAY: delay ≥ 32 samples, gain ≤ 0.95, damping, DC blocker, tanh. OUTPUT: then the
master level and a safety limiter (instant attack to −0.26 dBFS, 120 ms release, clip at ±1,
non-finite samples zeroed).

## Assets
Loader thread: decode (WAV here; WAV/AIFF/FLAC in the plugin via JUCE) → OSP `Analyzer` →
`buildProvisional` (published: playable) → `addContinuation` (published: sustain loop). Tempo:
onset-flux autocorrelation 70–180 BPM, preferring whole beats over the loop.
