# Status — ANDOR Modular prototype

_Last updated 2026-11-12. Branch `claude/epic-feynman-fsf8ij`. Pins: OSP `e7a3834`, Analog `1c0cb6d`._

## Scoreboard

| Gate | State | Commit | Evidence | Blocker |
|---|---|---|---|---|
| 0 Survey & source protection | done | 282038e | docs/00_DISCOVERY_AUDIT.md (SHAs, unmerged branches, reuse table, risks); Analog + OSP headless baseline 6/6 passed | — |
| 1 Repository & identity | done | 282038e | product/product.json; configure-time uniqueness check vs OSP registry + Analog; CI workflow (Linux/macOS kernel + plugin, product tests under Xvfb, auval on macOS; Windows pending SQLite3 for OSP) | OSP registry entry (separate OSP PR) |
| 2 Graph kernel & first sound | done | 282038e | typed document, validator, compiler (lanes, units, buffers, latency), engine (grid, swaps, voices); EngineTests pitch / voices / determinism / hot swap | — |
| 3 Sources & processors | done | 282038e | 6-source budget, ANALOG OSC ×3 models, NOISE, FILTER (ladder/OTA/SVF), envelopes, mixers, gain/width, FX; Six Oscillator Stack | — |
| 4 Organic, granular, rhythm | done (Key DNA open) | 282038e | async loader (provisional → continuation), ORGANIC at pitch, GRANULAR with live grains, RHYTHMIC SOURCE (120 BPM found), follower → filter/VCA (Clockwork) | Key DNA multi-zone (P2) |
| 5 MORPH & advanced processors | done (REIMAGINED unavailable) | 282038e, this commit | per-voice MORPH (ends exact, middle ≠ crossfade); GLOBAL MORPH (new, staged, latency aligned); DRIVE, SPACE, ECHO, MOVEMENT, chorus, phaser, flanger, shifter, compressor, retro, spread, convolution | REIMAGINED (P2) |
| 6 Six arps & generative | done | 282038e | 6 arps budget, merge with distinct ids, STEP SEQUENCER on the grid, GENERATIVE seeded (identical re-render), CLOCK, LFO, RANDOM, scale quantise | — |
| 7 SATELLITE & routing | done | 282038e | PAN/ORBIT/BOUNCE, mono-coherent; MIXER, sends via mixers, FEEDBACK DELAY bounded, latency null test, scopes/meters | validated branch unmerged (re-implemented) |
| 8 PATCH UX | done | c110afa, ANDOR look: this commit | board, Quick Add (< 100 ms to a playable graph, product test), insert on cable/port/selection, drop-to-morph, file drop, detail column, undo, groups; the ANDOR interface (walnut, ceramic, enamel tiles; docs/UI_COMPONENTS.md, D-16…D-19); screenshots 869/1448/1810 | owner visual review |
| 9 Instrument builder & presets | done | 282038e, c110afa | groups → .andormodule → import, group controls ← macros, PLAY panel, 12 patches + 6 modules | owner listening review |
| 10 Scenes & EVOLVE | done | 282038e, c110afa | 4 scenes + fader (host parameter), deterministic EVOLVE with locks, A/B, one undo | EVOLVE taste curve (owner) |
| 11 External instrument | partly: host built, surrogate-tested | c110afa | ExternalHost (VST3/AU from file, state in patch, window, missing fallback); ExternalTests: one instance, overlaps in order, GLOBAL MORPH, absence, edits | **live Kontakt not available here** (P1) |
| 12 QA, performance, docs | done for this environment | this commit | 43 kernel + 5 product test cases green; 240-configuration benchmark, 0 allocations; docs set; demo renders script | reference Mac numbers, DAW validation (P1) |

## What works (demonstrable now)
- PLAY is Analog's front panel: a card for each source with its real controls (D-20).
- The interface is the ANDOR family's own (walnut housing, ceramic keys, ivory macro knobs,
  Analog's device enamels on the board), scaled as one piece from 869 to 1810 px wide; its
  keyboard, PITCH wheel and VOLUME play and set the patch.
- Empty patch → Quick Add "analog" → Return → sound, inside the real editor, in < 100 ms.
- All 53 module types (52 available; REIMAGINED listed unavailable with its reason).
- Independent note streams per chain, merged streams without stuck notes, mono/legato, steals.
- Edits while notes hold: click-free swaps, held keys continue.
- Every factory patch plays in the plugin with ANDOR's own recordings; state recalls.
- Output identical across host block sizes; zero heap allocations in `process()`.

## What does not (yet)
See [RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md): live Kontakt, DAW validation, Mac numbers,
per-voice MORPH at tiny buffers with many voices, REIMAGINED, Key DNA, topology scenes,
mini-map, free macro range sliders.

## NEXT
1. Owner: listen to the demo renders; review the new screenshots (docs/screenshots).
2. Run Demo 10 with Kontakt on the owner's Mac; record the result here.
3. Analog PR: LiveMorph `start` with an analysis phase (stagger voices) — then re-measure.
4. OSP PR: add Modular to the product registry.
5. OSP PR: split the keyboard, wheel, VOLUME and preset bar from OSP's parameter bindings so
   Modular links them instead of re-binding their recipe (D-16).
6. Merge the Satellite / Rhythmic Morph branches upstream, then port their DSP into
   `spatial.satellite` and `src.rhythm` (keeping these modules' ids and parameters).
