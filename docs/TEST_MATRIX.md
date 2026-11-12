# Test matrix

Kernel: `build/tests/modular_tests` — **63 test cases, 1,970,974 assertions, all passing**
(2026-11-12, Linux, GCC 13.3; CI also macOS 15, Apple clang). Product: `modular_product_tests`
— **18 cases, all passing** (headless under Xvfb).

| Area | Test (file: case) | What it proves |
|---|---|---|
| JSON | JsonTests: round trip; hostile input + 3000 random mutations of a patch | deterministic writer; parser never crashes, depth/size limits |
| Registry | GraphTests: every module registered completely | names, ranges, defaults round-trip, structural ⇒ not modulatable, unavailable ⇒ reason |
| Quick Add search | GraphTests: search | "analog", "filter", "reverb", "arp", fuzzy "anlg osc" |
| Validation | GraphTests: types, domains, fan-in, cycles, budgets | refusals with converter suggestions; fan-in into audio inputs accepted, a second cable into MORPH's A refused; FEEDBACK DELAY allowed; broadcast marked |
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
| Silent recordings | ReliabilityTests `[organic][live]` | a recording arriving under a held key; one voice / mono / legato / an ended one-shot; START and REVERSE on the first note; **no clear pitch → C4 plays it as recorded** |
| Note lifecycle | ReliabilityTests `[event][transpose][lifecycle]` | OCTAVE under held keys (no move, no hang, no rebuild, no click); out-of-range and split keys released; through an ARPEGGIATOR nothing is left running; KEY SPLIT and SCALE / TRANSPOSE |
| Mute, solo, bypass | ReliabilityTests `[source][mute][solo]` | ramps at 44.1 / 48 / 96 kHz × blocks 16 / 64 / 512: no rebuild, no click, no restarted note; solo isolates at the source's output; mute wins; groups; save / load |
| Shared routing | ReliabilityTests `[routing][fanin]` | two and three sources through one per-voice filter (one instance per voice, both darkened); two arpeggiated chains through one global filter (two voice pools, one instance, nothing hangs); voice audio into a global SPACE; refusals leave the patch untouched |
| Layout | ReliabilityTests `[ui][layout]` | every factory patch tidy; drop / insert / six sources; 40-seed property test (no overlap, deterministic, idempotent, anchors stay); 32 stacked modules in 0.6 ms |
| Source type | ReliabilityTests `[source][type]` | in place (id, name, place, M / S); each type's settings and recording come back; cables and macro / host assignments only one type has are set aside and restored, or taken back by a type that can use them; a type that plays once joins after the voices, and back; refusals (not a source, a second hosted instrument) leave the patch untouched; save / load round trip; a held chord through five changes |
| Real time | RealtimeTests: every factory patch × blocks 32/333/512 with notes, stealing, bend, macro and scene moves and three graph swaps | **zero** heap calls in `process()` |
| External | ExternalTests (surrogate instrument) | one instance for all notes (not × polyphony), legato overlaps in order, GLOBAL MORPH hears it, absent plugin silent and kept, survives edits |
| Product | ProductTests: every factory patch plays in the processor; fixed parameter ids; state recall (host values win); 0-to-sound through Quick Add < 100 ms + insert FILTER + one undo; screenshots 869/1448/1810 | |
| Product: recordings, M / S | `[organic][live]`, `[source][mute][solo]` | a non-ASCII path and name recall; a Quick-added sample source sounds at once; M / S / the light never swap the graph; one undo each |
| Product: Quick Add | `[ui][quickadd]` | opens in < 50 ms; six categories in order; root 246–292 px wide and short; submenu beside it, inside the instrument; every module in exactly one category; search ≤ 8; the keyboard path; one Return = one module; recent; port filtering |
| Product: Escape | `[ui][escape]` | the whole stack built, then closed one level per press in order, by method and through the keys; no history entry; leaving a group |
| Product: rack | `[ui][rack]` | full width under the canvas, 210–290 px; the canvas keeps x, y, width and zoom; identity / controls / display in order; every control of the mode; swap; close; deleted module closes it; PLAY hides it; no graph swap |
| Product: layout | `[ui][layout]` | twelve modules through the menu onto one spot: no overlap, undo and redo exact; the make-room glide starts and ends; a dropped tile stays |
| Product: gesture | `[ui][gesture]` | pan follows the deltas whatever `isReversed` says; Traditional reverses; scrolling never zooms, Cmd + scroll does; F changes nothing; FIT fits |
| Product: native racks | `[ui][rack][native]` | ECHO is the family's rack, two echoes independent, no graph swap, FEEDBACK changes the tail; SPACE's four engines and MOVEMENT's modes play in the module, SHAPER's phase live, a drawn pattern one undo step; every available module type opens a device whose cells stay inside it on every page |
| Product: Source | `[ui][source][type]`, `[source][type][state]` | SOURCE first in Sources and found by ten words; added in < 100 ms; every type sounds from the card; undo and redo exact; the rack follows; six sources; factory files load and save byte for byte, each card shows its type |
| Product: finish | `[ui][materials]` | the pack is Modular's; cheeks, rails and ceramic sampled at three sizes |

## Not covered yet (see RELEASE_BLOCKERS.md)

Host validation (pluginval, auval run only in CI on macOS, Logic/Live/Reaper sessions), a live
Kontakt, five-musician usability sessions, Windows (kernel and product), accessibility audit with a
screen reader, ASan/UBSan run (`-DMODULAR_SANITIZE=ON` exists, not yet in CI).
