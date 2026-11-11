# Performance budget

## Budgets enforced (`Budgets`, dsp/graph/Validator.h)

| Limit | Value | Behaviour |
|---|---|---|
| Sound sources (flattened, groups included) | 6 | Quick Add / import / duplicate refuse with the count |
| Modules | 32 | refused |
| Connections | 64 | validation error |
| Arpeggiators | 6 | refused |
| MORPH modules (voice + global) | 2 | refused |
| Modulation routes | 32 | refused |
| External instruments | 1 | refused |
| Group nesting | top level + 1 | refused |
| Cost (Σ cost units × polyphony for voice modules) | 120 | warning in the status bar ("may exceed the CPU budget") |
| Runtime | — | sustained CPU load > 85 % trims the voice cap (oldest notes stolen first) |

Cost units are relative to one CLASSIC oscillator voice (1.0): MORPH 6, GLOBAL MORPH 3,
GRANULAR 2.5, EXTERNAL 4, SPACE 2, CONVOLUTION 2, FILTER 0.8, DRIVE 1.2, NOISE 0.3, controllers
0.05. Calibrated from the matrix below; tighten as the reference Mac numbers arrive.

## Measured matrix (2026-11-11)

Full table: [reports/bench-2026-11-11.md](reports/bench-2026-11-11.md) — 5 patches × 3 rates ×
4 block sizes × 4 polyphonies = 240 configurations, every callback timed, chords re-struck
every second. **Platform:** Linux x86-64 cloud container (4 vCPU, shared, no pinning), GCC 13.3
Release. **Not yet measured on the primary reference (Apple Silicon)** — CI runs the quick
matrix on macOS-14 (see .github/workflows/ci.yml).

**Heap allocations inside `Engine::process`: 0 in all 240 configurations** (and in
`tests/RealtimeTests.cpp`, which also swaps graphs and moves macros and scenes mid-chord).

p99 callback time as % of the deadline — cells: block 32 @ 1 voice / 16 voices, block 512 @ 1 / 16:

| Patch | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Basic Poly (1 osc, filter, env, SPACE) | 9 / 10 / 7 / 12 | 11 / 11 / 10 / 14 | 20 / 25 / 18 / 20 |
| Three Arps (3 lanes, SATELLITE, ECHO) | 3 / 5 / 2 / 6 | 3 / 6 / 2 / 5 | 7 / 19 / 4 / 7 |
| Satellite Orchestra (3 lanes, 3 SATELLITEs, SPACE) | 4 / 11 / 3 / 8 | 4 / 12 / 2 / 7 | 9 / 17 / 5 / 13 |
| Six Oscillator Stack (6 oscs × voices, OTA, chorus) | 3 / 12 / 3 / 14 | 4 / 18 / 3 / 15 | 7 / 38 / 5 / 32 |
| Two-Morph Dream (MORPH per voice + GLOBAL MORPH + granular) | 29 / **173** / 6 / 36 | 23 / **184** / 7 / 51 | 45 / **395** / 19 / 69 |

Compile time (Document → CompiledGraph, message thread): 0.1–6 ms for every factory patch.
Offline render speed: 8–75 × real time per patch at 48 kHz (`tools/render-demos.sh`).

## Reliable limits (what to tell musicians)

- Everything except per-voice MORPH runs comfortably at 16 voices, 32-sample blocks, 96 kHz.
- **Per-voice MORPH**: ANDOR Analog's LiveMorph analyses every voice of a chord in the same
  block (they start together), so small blocks see the whole chord's FFTs at once. Measured safe:
  ≤ 4 voices at 32-sample blocks, ≤ 8 at 64, 16 at ≥ 256 (48 kHz). The cost model warns above
  this; the runtime cap trims voices under sustained overload.
  **Planned fix (needs an Analog change, not made from here):** stagger each voice's first
  analysis by its voice index in LiveMorph (an `analysisPhase` argument to `start`).
- **GLOBAL MORPH** spreads its four transforms over the hop (one per quarter): its own cost per
  32-sample block is a quarter of a frame (1 voice @ 48 kHz / 32: 57 % → 23 % after the change).
- Hosted plugins are measured by their own makers; their cost is not bounded by Modular.

## Not measured yet

UI menu latency on the reference machine (product test: Quick Add → playable graph < 100 ms
in the headless build, checked every CI run), RSS per recording (a 4.5 s mono recording is
~1 MB decoded + the continuation model), 6-arp and 16-voice granular stacks (patches to add to
the matrix), Windows timings.
