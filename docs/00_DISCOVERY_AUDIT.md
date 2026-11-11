# 00 — Discovery audit (Gate 0)

_Date: 2026-11-11. Audited from fresh clones; every SHA below was read from `git`, not assumed._

## 1. Upstream states

| Repository | Default branch (remote HEAD) | Commit | Notes |
|---|---|---|---|
| arenttodal/OSP | `claude/charming-lovelace-n25fte` | `0a418b9` "Pedal Loop 1.0 (#13)" | 38 commits ahead of Analog's pin; adds the shared FX library under `osp::fx`, Pedal Loop |
| arenttodal/analog | `claude/upbeat-allen-3i0jey` | `1c0cb6d` "SUB follows the FILTER macro and MOVEMENT…" | pins OSP at `e7a3834` (branch `claude/analog-release` + 11) |
| arenttodal/analog `main` | — | `4bc6c41` (1.0.0 release candidate merge) | pins OSP at `adf8ed6` |
| arenttodal/andor-modular | — | empty at start | this product |

Work in progress that is **not merged** (checked, not assumed):

| Branch | Repo | Head | What it holds | Used by Modular? |
|---|---|---|---|---|
| `feature/satellite-rhythm` | analog | `c7b1bc9` | SATELLITE (pan/orbit/bounce) + RHYTHMIC MORPH gates 1–8, demo renders; pins OSP `41edc0b` | No: SATELLITE re-implemented from audio primitives in Modular (`spatial.satellite`), level-based, mono-coherent (spec §5.4 fallback). Port the feature branch once merged. |
| `feature/kontakt-external-morph` | analog | `a78656b` | Kontakt experiment gates 0–6: VST3 host bridge for card C, surrogate instrument, GLOBAL MORPH (spectral imprint) | No code taken; its design (one global instance, surrogate test instrument, missing-plugin fallback) is followed in `src.external` + `product/Source/ExternalHost.*` + `tests/ExternalTests.cpp`. |
| `feature/user-macros-v1`, `feature/sub-popup-v7`, `feature/andor-learn-v1` | analog (and OSP) | — | macro bank UI hooks, SUB popup, learn | Not needed by Modular. |
| `feature/kontakt-external-morph`, `feature/satellite-rhythm` | OSP | `41edc0b` | shared C RHYTHM source plumbing | No. |

**Pins chosen for Modular** (`git submodule status`):

- `external/osp` = `e7a3834f4fa33b96d6a5ee4357622e39927f8477` — exactly ANDOR Analog's own pin, so Analog's DSP compiles against the OSP it is tested with.
- `external/analog` = `1c0cb6df4936315a18ff92bf84c8fbf58d2a4641` — Analog's default branch.
- Submodules are initialised **non-recursively** (Analog's nested `external/osp` is not used: one OSP in the build).

## 2. Baseline builds and tests (before any Modular code)

`cmake -B build -G Ninja && cmake --build build && ctest` in `arenttodal/analog` @ `1c0cb6d` (it builds OSP @ `e7a3834` as a subproject), Linux x86-64, GCC 13.3, 4 cores:

| Suite | Result | Time |
|---|---|---|
| analog_dsp_tests | Passed | 135.9 s |
| OSP unit | Passed | 533.7 s |
| OSP integration | Passed | 924.0 s |
| OSP regression (goldens) | Passed | 265.9 s |
| OSP performance-smoke | Passed | 1.1 s |
| OSP andor_bootstrap_tool | Passed | 0.1 s |

**6/6 passed.** 155 compiler warnings, all in upstream code (not touched). Plugin targets of OSP/Analog were not rebuilt here (their own CI covers them; Modular never modifies them). No upstream goldens were changed.

Baseline performance of OSP/Analog scenes at 44.1/48/96 kHz was **not** captured as a separate matrix in this session: Modular's own matrix (`tools/modular-bench`, docs/PERFORMANCE_BUDGET.md) measures the reused DSP inside Modular's engine, which is the number that matters for Modular's budgets.

## 3. Reuse table

Columns: where it lives (pinned SHAs above), public API, dependence on a monolith, real-time guarantee, domain, tests upstream, licence/assets, extraction strategy, modification risk.

| Component | Path | Public API used | Monolith dependence | RT-safe | Domain in Modular | Upstream tests | Licence / assets | Strategy | Risk |
|---|---|---|---|---|---|---|---|---|---|
| Oscillator models CLASSIC / DCO / WIDE (+SUPER SAW) | analog `dsp/Oscillator.h` | `osc::renderModel`, `wavesOf`, `SlotPlan`, `SlotExtra`, `superSaw*` | header-only; includes `Patch.h` for enums only | yes (no alloc) | voice | OscillatorTests, ModelTests | ANDOR | compile header from pin, no copy | low |
| BLEP/BLAMP corrections | analog `dsp/PolyBlep.h` | `BlepLine::step/corner/end` | none | yes | voice | OscillatorTests | ANDOR | header from pin | low |
| Ladder filter | analog `dsp/LadderFilter.h` | `process (x, g, k, bias, offset)` | none | yes | voice/global | FilterTests | ANDOR | header from pin | low |
| OTA (Juno/Jupiter) filter | analog `dsp/OtaFilter.h` | `process (x, g, res, character, twoPole)` | none | yes | voice/global | OtaFilterTests | ANDOR | header from pin | low |
| HPF positions | analog `dsp/HighPassFilter.h` | `VoiceHighPass` | none | yes | — | FilterTests | ANDOR | not used (12 dB SVF HIGH mode instead) | — |
| Envelope (ADSR, capacitor shapes) | analog `dsp/Envelope.h` | `setTimes`, `noteOn/Off`, `next`, `isActive` | none | yes | voice/global | EnvelopeTests | ANDOR | header from pin | low |
| LFO / mod matrix | OSP `src/engine/Modulation.h` | — | tied to OSP's Dest/Source tables | yes | — | OSP unit | ANDOR | **not reused**: Modular's modulation is graph edges; its LFO/RANDOM are small new nodes with seeded `osp::Prng` | — |
| Sample loading + analysis | OSP `src/analysis/Analyzer.*`, `src/engine/InstrumentBuilder.*`, `model/*` | `Analyzer::analyse`, `instrument::buildProvisional / addContinuation` | none (pure C++) | off-thread only | loader thread | OSP unit/integration | ANDOR | linked from `osp_dsp` | low |
| Organic per-voice playback | analog `dsp/OrganicVoice.h` | `start`, `stepFor`, `next`, `playhead` | header-only | yes | voice | OrganicTests | ANDOR | header from pin | low |
| Granular engine | OSP `src/engine/GranularSource.*` | `start`, `setParams`, `render`, `collect` | none | yes | voice/global | OSP unit | ANDOR | linked from `osp_dsp` | low |
| Live granulator | analog `dsp/LiveGranulator.*` | `LiveRecording`, `LiveGranulator` | none | yes | — | LiveTests | ANDOR | compiled, not wired yet (granular processor of a live stream: NEXT) | — |
| Key DNA | OSP `src/engine/KeyDna.*` | — | OSP sample engine | — | — | OSP unit | ANDOR | not wired (needs multi-zone sets): RELEASE_BLOCKERS | — |
| REIMAGINED | OSP `src/engine/ReimaginedStage.*` and engines | — | needs per-recording `ReimaginedAnalysis` inside OSP's engine | yes | — | OSP | ANDOR | **listed unavailable** with its reason | — |
| LiveMorph (per voice) | analog `dsp/morph/LiveMorph.*` | `prepare (rate, os, shared)`, `start`, `control`, `process (x, y)` | `Patch.h` enums only | yes after prepare | voice | MorphTests, LiveMorphTests | ANDOR | compiled from pin, oversampling 1 | low |
| Global bus morph | — (Kontakt branch has SpectralImprint, unmerged; not in OSP @ e7a3834) | — | — | — | global | — | — | **new** `proc.morph.global` (STFT 2048/512, IMPRINT + FUSE, exact ends, latency reported) | medium |
| Arpeggiator | OSP `src/engine/Arpeggiator.*` | `beginBlock/advanceTo/noteOn/noteOff/endBlock/event/display` | none | yes | event | OSP unit | ANDOR | linked; one instance per ARP node | low |
| PostProcessor (drive/movement/echo/space) | OSP `src/engine/PostProcessor.*` | — | a fixed chain | yes | — | OSP | ANDOR | the chain itself not reused: its stages are | — |
| DRIVE | OSP `src/engine/DriveProcessor.*` | `Settings`, `process (l, r)` | none | yes | voice/global | OSP unit | ANDOR | linked | low |
| SPACE (rooms) / ABYSS | OSP `SpaceReverb.*`, `AbyssReverb.*` | `configure`, `tune`, `process` | none | yes | global | OSP unit | ANDOR | linked | low |
| ECHO (TAPE/BBD/ASCEND) | OSP `EchoDelay.*` | `setSettings`, `setTempo`, `process` | none | yes | global | OSP unit | ANDOR | linked | low |
| MOVEMENT | OSP `MovementBus.*` | `setTargets (Shaping, amount)`, `process` | reads `Shaping` | yes | global | OSP unit | ANDOR | linked (SHAPER mode not offered: needs note events) | low |
| Ensemble chorus | analog `dsp/JunoChorus.*` | `setMode`, `setNoise`, `process` | none | yes | global | ChorusTests | ANDOR | compiled from pin | low |
| Routed FX library (phaser, flanger, shifter, compressor, FX filter, retro, spread, convolution) | analog `dsp/fx/*` | `makeProcessor (Type)`, `Processor::set/process/latency` | `FxEngine` not used | yes | global | FxGraphTests, FxEffectTests, FxRetroTests | ANDOR; factory impulses synthesised | compiled from pin | low |
| Graph / latency planner | analog `dsp/fx/FxGraph.*`, `FxEngine.*` | — | post-voice only | — | — | FxGraphTests | ANDOR | design precedent only: Modular's compiler plans voice/global/event units and aligns latencies itself | — |
| Satellite | analog `feature/satellite-rhythm` (unmerged) | — | — | — | — | SatelliteTests (branch) | ANDOR | re-implemented (see above) | medium |
| Rhythm analysis | analog branch (unmerged); OSP `OnsetDetector` | — | — | — | — | — | — | new, small: `AssetLibrary::analyseRhythm` (onset flux + autocorrelation tempo) | medium |
| External SoundSource / Kontakt host | analog `feature/kontakt-external-morph` (unmerged) | — | — | plugin's own | global | ExternalInstrumentTests (branch) | Kontakt licence is the user's | new `ExternalHost` (JUCE `AudioPluginFormatManager`), one instance, state in patch | high (third-party code) |
| ModelExchange (lock-free hand-over) | OSP `src/model/ModelExchange.h` | publish / takePending / collectGarbage | none | yes | — | OSP unit | ANDOR | pattern followed: `Engine::publish` (atomic pending pointer + lock-free retire ring, freed on the message thread) | low |
| Graphical components / shell | OSP `shared/andor-ui`, `shared/andor-shell` | InstrumentHost, EngineCard, … | **three-card product shell** | — | — | design-capture | ANDOR; fonts OFL | **not linked**: Modular's canvas is a different product shape (spec: do not inherit the three cards). Typefaces (Inter, Outfit, OFL) are taken from `shared/andor-ui/Resources/fonts` | low |
| Product registry | OSP `andor-design-system/product-registry.json` | — | — | — | — | — | — | read at configure time to prove Modular's identifiers are unique; **not edited** | low |
| Automation / preset schema | OSP `AndorParameters`, Analog `PARAMETER_REGISTRY.md` | — | per-product fixed parameters | — | — | golden schema | — | Modular uses its own fixed bank (8 macros, scene, 32 assignable slots) with stable ids | low |

## 4. Risk register

| Risk | Likelihood | Impact | Mitigation in place |
|---|---|---|---|
| Upstream API drift when the pins move | medium | build break | pins are explicit; Modular compiles only the files it lists; CI builds every push |
| Analog's `Patch.h` pulls OSP headers (`engine/*`) | certain | compile coupling | the OSP pin equals Analog's pin; checked by the build |
| Hosted plugins break real-time safety | high | dropouts | isolated in `ExternalHost`; documented in RELEASE_BLOCKERS; subprocess hosting is a later hardening option |
| 16-voice granular / morph stacks exceed CPU | medium | dropouts | cost model + budget warning; runtime voice cap under sustained load; measured in PERFORMANCE_BUDGET |
| Global morph is new DSP | medium | sound quality | exact-endpoint and latency null tests; listening review pending (owner) |
| JUCE licence for distribution | certain | release | prototype only; see LICENSING.md |
