# Status — ANDOR Modular prototype

_Last updated 2026-11-12. Branch `claude/epic-feynman-fsf8ij`. Pins: OSP `c21fe80`, Analog `40bf866` (both on their repository's `claude/epic-feynman-fsf8ij`: `e7a3834` and `1c0cb6d` plus the changes below)._

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
| 8 PATCH UX | done | c110afa; workflow pass 4a277b6 | board, compact Quick Add (< 100 ms to a playable graph), insert on cable/port/selection, drop-to-morph, file drop, the rack, undo, groups, layout rule; the ANDOR interface (ceramic, enamel tiles; docs/UI_COMPONENTS.md, D-16…D-27); screenshots 869/1448/1810 | owner hands-on review (docs/PATCH_WORKFLOW_REPORT.md) |
| 9 Instrument builder & presets | done | 282038e, c110afa | groups → .andormodule → import, group controls ← macros, PLAY panel, 12 patches + 6 modules | owner listening review |
| 10 Scenes & EVOLVE | done | 282038e, c110afa | 4 scenes + fader (host parameter), deterministic EVOLVE with locks, A/B, one undo | EVOLVE taste curve (owner) |
| 11 External instrument | partly: host built, surrogate-tested | c110afa | ExternalHost (VST3/AU from file, state in patch, window, missing fallback); ExternalTests: one instance, overlaps in order, GLOBAL MORPH, absence, edits | **live Kontakt not available here** (P1) |
| 12 QA, performance, docs | done for this environment | this commit | 68 kernel + 20 product test cases green; 240-configuration benchmark, 0 allocations; docs set; demo renders script | reference Mac numbers, DAW validation (P1) |

## Patch workflow, audio reliability, shared routing and material pass (2026-11-12)

Report with root causes, measurements, limits and the owner's checklist:
[PATCH_WORKFLOW_REPORT.md](PATCH_WORKFLOW_REPORT.md).

| Gate | State | Commit | Evidence |
|---|---|---|---|
| 0–1 Silent ORGANIC SAMPLE, octave note lifecycle, clicks | done | d5626a1 | `[organic][live]`, `[event][transpose][lifecycle]` (fail on the old code) |
| 2 Mute and Solo | done | d5626a1 | `[source][mute][solo]`: ramps in the running graph, no rebuild |
| 3 Several sources into one filter / effect | done | 3b483b1 | `[routing][fanin]`: per voice (summed) or once, globally |
| 4 Escape, scrolling, F | done | 4a277b6 | `[ui][escape]`, `[ui][gesture]` |
| 5 Compact Quick Add | done | 4a277b6 | `[ui][quickadd]` |
| 6 Rack | done | 4a277b6 | `[ui][rack]`; a filter's display is its level, not a response curve (open) |
| 7 Layout | done | 83cdf32, 4a277b6 | `[ui][layout]` (property test, every factory patch, through the menu) |
| 8 Finish | done, not compared with the reference image | 363d551 | `[ui][materials]`; docs/MATERIALS.md |
| 9 Hands-on validation | **open: needs a person, a Mac and a DAW** | — | the checklist in the report |

CI (Linux, macOS 15: both suites, auval, installer): green for d5626a1 and 3b483b1; the later
commits' runs are listed under the repository's Actions.

## Addendum 02: native ANDOR racks and the one flexible Source (2026-11-12)

Report: [ADDENDUM_02_REPORT.md](ADDENDUM_02_REPORT.md); what is the family's own code and
what is built from its parts: [ADDENDUM_02_REUSE_AUDIT.md](ADDENDUM_02_REUSE_AUDIT.md).

| Gate | State | Commit | Evidence |
|---|---|---|---|
| A Audit | done | bb68057 | the audit; "just call Analog's factories" shown not to work |
| B Rack binding, ECHO | done | bb68057 | ANDOR's `EchoRack` on a node through `RackHost`; `[ui][rack][native]` |
| C Rack library | done, with one stated gap | 3275618, f86d955 | SPACE and MOVEMENT are the shared racks; every other module has a device built from the family's parts. **Gap:** FILTER, MORPH and the effects are not Analog's own rack classes (they need an upstream seam) |
| D One SOURCE | done | d63b386 | Quick Add SOURCE; TYPE on card, tile menu and rack |
| E Type change | done | d63b386 | `edit::changeSourceType`: in place, nothing lost, refusals; kernel and product `[source][type]` |
| F Integration | done here; hands-on open | this commit | kernel 63, product 18 cases; paired pictures; the report's nine hands-on checks |

## Addendum 02, second pass: Analog's own effect racks, MIX, live traces, own presets (2026-11-12)

The owner's four choices from the first report, built. Details and limits:
[ADDENDUM_02_REPORT.md](ADDENDUM_02_REPORT.md), "Second pass".

| What | State | Where | Evidence |
|---|---|---|---|
| Upstream seam: Analog's FX rack reads its instrument through `FxRackSource` only; the FX parameter table in the JUCE-free engine library; settable preset root | done | Analog `40bf866` | Analog `[fx][parameters]`, `[analog][fx][rack][seam]`; Analog's and OSP's complete suites (ctest) |
| Shared racks: opt-in MIX knob (ECHO, SPACE), preset folder setter | done | OSP `c21fe80` | default unchanged (OSP's suites and visual regressions); exercised in Modular `[rack][native][live]` |
| Effect modules carry the whole library table (schema 2, migration) | done | this commit | kernel `[fx][table]`, `[state][fx]`; old vs new renders bit-identical (STATE_SCHEMA.md) |
| PHASER, FLANGER, FREQUENCY SHIFTER, COMPRESSOR, FX FILTER, STEREO SPREAD, RETRO, CONVOLUTION open Analog's rack class: pages, modes, presets, meters | done | this commit | product `[rack][native][fx]`; `docs/screenshots/parity/` |
| MIX inside the ECHO and SPACE racks (and on the tile) | done | this commit | product `[rack][native][live]` |
| FILTER live trace; MORPH result trace | done | this commit | product `[rack][native][live]`; `rack-proc-filter-live-1448.png` |
| Presets in ANDOR Modular's own folders | done | this commit | product `[rack][native][fx]` (paths); D-38 |
| Hands-on (Mac, DAW, listening) | **open** | — | the report's checks |

## What works (demonstrable now)
- Double-click any module: its ANDOR device opens along the board's foot (ECHO, SPACE and
  MOVEMENT are the family's very racks). A source is one SOURCE whose TYPE changes on its card.
- Quick Add is a small menu (search, recent, six categories with submenus); a module's controls
  open in a rack across the board's foot; Escape closes one thing at a time; modules never
  land on each other; M and S on sources and groups; several sources share one filter.
- PLAY is Analog's front panel: a card for each source with its real controls (D-20).
- The interface is the ANDOR family's own (ceramic keys, ivory macro knobs, in Modular's
  ebonized ash and black aluminium trim,
  Analog's device enamels on the board), scaled as one piece from 869 to 1810 px wide; its
  keyboard, PITCH wheel and VOLUME play and set the patch.
- Empty patch → Quick Add "analog" → Return → sound, inside the real editor, in < 100 ms.
- All 53 module types (52 available; REIMAGINED listed unavailable with its reason).
- Independent note streams per chain, merged streams without stuck notes, mono/legato, steals.
- Edits while notes hold: click-free swaps, held keys continue.
- Every factory patch plays in the plugin with ANDOR's own recordings; state recalls.
- Output identical across host block sizes; zero heap allocations in `process()`.

## macOS build (2026-11-12)
CI builds the plugin universal (arm64 + x86_64, macOS 11+) on `macos-15` and uploads two
downloads from every successful build: `ANDOR-Modular-macOS-installer` (one `.pkg`: AU, VST3,
standalone; UNSIGNED until the repository has the Apple secrets named in
`packaging/macos/build-installer.sh`) and `ANDOR-Modular-macOS-test` (a zip with
`install.command`: per-user, no admin rights, clears the quarantine). On the runner: kernel
suite, product suite and `auval -v aumu Amdr Ospx` pass. Not yet done on a real Mac by a
person: installing, a DAW session, listening.

## What does not (yet)
See [RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md): live Kontakt, DAW validation, Mac numbers,
per-voice MORPH at tiny buffers with many voices, REIMAGINED, Key DNA, topology scenes,
mini-map, free macro range sliders.

## NEXT
0a. Owner: the hands-on checks in ADDENDUM_02_REPORT.md (both passes).
0b. Analog (its own decision): the FX parameters meant to be linear are skewed whenever
   their range contains -1, the table's "no centre" marker (COMPRESSOR THRESHOLD most of all:
   nearly all of its knob travel is the top 6 dB). Found while binding the rack; not changed
   here because it changes how hosts' automation maps. See the report.
0c. When Analog moves to OSP's shared FX line (OSP default branch: `shared/andor-ui/FxRack`),
   move both pins and switch `RackHost` from `FxRackSource` to the host's own `fxProcessor`.
0. Owner: the twelve hands-on checks in PATCH_WORKFLOW_REPORT.md (Mac, DAW, trackpad,
   the Concept 2 image beside the plugin); then a next pass from what was heard and felt.
1. Owner: listen to the demo renders; review the new screenshots (docs/screenshots).
2. Run Demo 10 with Kontakt on the owner's Mac; record the result here.
3. Analog PR: LiveMorph `start` with an analysis phase (stagger voices) — then re-measure.
4. OSP PR: add Modular to the product registry.
5. OSP PR: split the keyboard, wheel, VOLUME and preset bar from OSP's parameter bindings so
   Modular links them instead of re-binding their recipe (D-16).
6. Merge the Satellite / Rhythmic Morph branches upstream, then port their DSP into
   `spatial.satellite` and `src.rhythm` (keeping these modules' ids and parameters).
