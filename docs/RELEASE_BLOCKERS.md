# Release blockers and known gaps

Status after this session. P0 = blocks the prototype definition; P1 = blocks a release; P2 = known gap.

## P0 — none open
Everything in the spec's "fully functional prototype" definition is demonstrable headless and in
the plugin, except the items below which depend on things this environment does not have.

## P1
| Item | Why open | Plan |
|---|---|---|
| Live Kontakt / VST3 instrument test (Gate 11 acceptance) | no licensed Kontakt (or any VST3 instrument) installed in this Linux container | `ExternalHost` loads a chosen VST3/AU; tests use a surrogate. Run Demo 10 on the owner's Mac: EXTERNAL INSTRUMENT → CHOOSE PLUGIN → Kontakt; play a legato phrase; save; reopen |
| Hands-on validation of the 2026-11-12 pass (audio fixes, M / S, shared filters, menu, Escape, rack, layout, scrolling, finish) | no person, Mac, DAW, trackpad or reference image in the build session | the twelve checks in PATCH_WORKFLOW_REPORT.md |
| Hands-on validation of Addendum 02 (racks beside Analog's, source types with a real recording and Kontakt, type change under held notes) | no person, Mac, DAW or reference screenshots in the build session | the nine checks in ADDENDUM_02_REPORT.md |
| Host validation | no DAW here | pluginval + auval on macOS CI (auval step present), Logic / Live / Reaper sessions on the owner's machine |
| Reference-machine performance (Apple Silicon) | measured on a shared Linux VM only | CI runs `modular-bench --quick` on macOS-14; full matrix on the owner's Mac |
| Per-voice MORPH at ≤ 64-sample blocks with ≥ 8 voices overruns | LiveMorph analyses a chord's voices in the same block | Analog PR: stagger first analysis by voice index (no behaviour change at the ends) |
| Owner listening review and visual review | taste | factory patches, GLOBAL MORPH, EVOLVE curve, canvas density (spec §12) |
| JUCE licence for distribution | JUCE is AGPLv3 or commercial | see LICENSING.md |

## P2
| Item | State |
|---|---|
| FILTER and MORPH as Analog's own rack classes | not possible as they are (Analog's FILTER rack is its whole voice, its MORPH graph is about card C); Modular's racks in Analog's identity, with a live trace (D-39). The effects **are** Analog's rack class now |
| DRIVE as Analog's rack class | Modular's sibling kept: Analog's has no OUTPUT |
| Pictures for CHORUS and TONE EQ | an output meter (Analog has no rack for either) |
| Analog's FX CHAIN presets | not offered: a chain is Analog's route, not a module |
| A modulation deep enough to hit the old end of one of ten widened ranges (schema 1 patches) | now carries on to the new end (STATE_SCHEMA.md) |
| COMPRESSOR THRESHOLD's knob travel | fixed in Analog (pin `9685c81`, arenttodal/analog#11): a plain dB scale. Open only until that pull request is merged upstream |
| A group's rack | the plain rack |
| Scenes per source type | a scene's values follow parameter names across types |
| Undo / redo of mute, solo and bypass | rebuilds the graph (the action itself is a 12 ms ramp); bypassing a note module rebuilds too |
| Rack state in the patch | not saved (closed when a patch opens); a rack's page and shown preset name are kept for the session only |
| Quick Add click-through | a click outside the menu closes it and is not passed on |
| The live accent under the new trim | still the copper from Analog's finish; owner decision |
| A 4× housing level | the pack has the shared 2× level only |
| REIMAGINED | listed as unavailable with its reason (needs OSP's per-recording analysis inside a module) |
| Key DNA multi-zone | not wired (single-recording GRANULAR / ORGANIC are) |
| Granular processor of a live stream | Analog's LiveGranulator compiled, no module yet |
| Topology-changing scenes | scenes interpolate parameters only (routing is never interpolated); scene capture of routing not offered |
| Mini-map, cable bundling, style studies (quiet / medium canvas) | not built |
| Macro range editing | presets (full, halves, middle third, inverted, curves) in a menu; no free min/max sliders yet |
| Panel layout choice | one row of eight (wide) / two rows of four (narrow or `panelLayout = 1`); no picker in the UI |
| Undo for knob drags that move many parameters (EVOLVE A/B uses undo/redo) | works; A/B is "one undo away" by design |
| Screen-reader names | components have titles; not audited with VoiceOver / NVDA |
| Windows | not in CI yet: OSP's library target needs SQLite3 (vcpkg) at configure time; the kernel itself is portable C++20 |
| OSP product registry entry for Modular | identity checked from here; the registry lives in OSP (separate PR) |
| Third-party plugin real-time safety | outside Modular's guarantee; subprocess hosting is a later hardening option |
