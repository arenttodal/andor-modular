# Test matrix

Kernel: `build/tests/modular_tests` — **43 test cases, 1,685,948 assertions, all passing**
(2026-11-11, Linux, GCC 13.3). Product: `modular_product_tests` — **5 cases, all passing**
(headless under Xvfb).

| Area | Test (file: case) | What it proves |
|---|---|---|
| JSON | JsonTests: round trip; hostile input + 3000 random mutations of a patch | deterministic writer; parser never crashes, depth/size limits |
| Registry | GraphTests: every module registered completely | names, ranges, defaults round-trip, structural ⇒ not modulatable, unavailable ⇒ reason |
| Quick Add search | GraphTests: search | "analog", "filter", "reverb", "arp", fuzzy "anlg osc" |
| Validation | GraphTests: types, domains, fan-in, cycles, budgets | refusals with converter suggestions; FEEDBACK DELAY allowed; broadcast marked |
| Budgets | GraphTests: seventh source refused | reason names the count |
| State | GraphTests: save/load lossless; missing module kept; atomic save; schema 0 migration; newer schema refused | |
| Compiler | GraphTests: lanes | two chains = two lanes; MORPH joins them |
| Pitch | EngineTests: 0-to-sound plays the played pitch | A2..A5 within 3 cents |
| Voices | EngineTests: polyphony / release / stealing; note ids; sustain pedal; MONO LEGATO glide | max voices = 4 with 8 notes; equal pitches from two streams independent |
| Determinism | EngineTests: deterministic and block-size independent | identical twice; identical at host blocks 1, 37, 64, 256, 512, 1000 |
| Hot swap | EngineTests: hot swap under held notes | no NaN, held note continues, no step larger than the saw's own edges |
| Latency | EngineTests: latent branch null test | 2560-sample GLOBAL MORPH path aligned with the dry path: residual < −80 dB |
| Feedback | EngineTests: FEEDBACK DELAY bounded | loop gain > 1 before the guard stays ≤ 1.0 and rings |
| Controls | EngineTests: macros, host automation, scenes | macro moves the cutoff ×10 in level; host slot; geometric scene interpolation |
| Nodes fuzz | NodeTests: every module, random settings, every domain | no NaN, peak ≤ 1 |
| Filter | NodeTests | ladder low-pass and HIGH 12 shapes |
| MORPH | NodeTests | ends exact (< −60 dB error), middle ≠ crossfade (> −10 dB from it) |
| GLOBAL MORPH | NodeTests | 0 % = A delayed by its latency (< −80 dB error); middle sounds |
| SATELLITE | NodeTests | balance swings > 0.8; mono downmix within 3 dB (ORBIT 9.5 dB by design: "behind") |
| ARP / STEP / GENERATIVE | NodeTests | stepped pitch; steps on the host grid; same seed same notes, other variation differs |
| ORGANIC SAMPLE | NodeTests | OSP finds C3; plays C3/G3/C4 within 20 cents (the recording's vibrato); missing file = MISSING |
| RHYTHMIC SOURCE | NodeTests | tempo 120 ± 2 BPM, 8 beats, ≥ 16 onsets; Clockwork's pad pulses with the ticks |
| Edits | EditTests: Quick Add chain / join / MIXER; insert on cable, after selection, from a port; undo/redo of smart actions; LAYER/MORPH/MODULATE/REPLACE | |
| Groups | EditTests: save as module, import elsewhere, macro → group control → inner cutoff; budget refusal | flattened source count |
| EVOLVE | EditTests | deterministic per seed, different per seed, valid, levels kept, protected modules untouched, one undo |
| Scenes / arrange | EditTests | interpolate, recall, choices switch at the middle; left-to-right arrange |
| Factory | FactoryTests: files = builder; all 12 play with recordings; 6 modules import; scene sweep keeps the chord | |
| Real time | RealtimeTests: every factory patch × blocks 32/333/512 with notes, stealing, bend, macro and scene moves and three graph swaps | **zero** heap calls in `process()` |
| External | ExternalTests (surrogate instrument) | one instance for all notes (not × polyphony), legato overlaps in order, GLOBAL MORPH hears it, absent plugin silent and kept, survives edits |
| Product | ProductTests: every factory patch plays in the processor; fixed parameter ids; state recall (host values win); 0-to-sound through Quick Add < 100 ms + insert FILTER + one undo; screenshots 869/1448/1810 | |

## Not covered yet (see RELEASE_BLOCKERS.md)

Host validation (pluginval, auval run only in CI on macOS, Logic/Live/Reaper sessions), a live
Kontakt, five-musician usability sessions, Windows (kernel and product), accessibility audit with a
screen reader, ASan/UBSan run (`-DMODULAR_SANITIZE=ON` exists, not yet in CI).
