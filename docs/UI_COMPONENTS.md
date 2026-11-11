# Interface components

Tokens (`product/Source/Theme.h`): Peat `#100D0C` ground, card `#1B1716`, card header
`#25201E`, hairline `#3B3430`, Bone `#E8E3DA` text (dim `#A39C92`, faint `#6E675F`), Spore
`#A8AE9C` for values/arcs/cables, ANDOR ember `#E8692A` only for live, selected and
modulation. 8 px grid; Inter (UI) and Outfit (identity), OFL. Port glyphs differ by shape
and colour (never colour alone). Focus is an ember outline on every control.

| Component | File | Notes |
|---|---|---|
| Editor | PluginEditor.* | top bar (PATCHES, name, UNDO/REDO, PATCH/PLAY, EVOLVE, POLY, ARRANGE, FIT, status); 869 px narrow layout; shortcuts Cmd/Ctrl+K (Quick Add), Z / Shift+Z, S, E, Ctrl+Tab |
| Canvas + NodeView | Canvas.* | world pan/zoom (75–125 % fit, 30–250 % manual, zoom at cursor, trackpad pinch), 8 px snap, rubber-band selection, Tab navigation, Delete, Cmd+D (+Shift: with links), Cmd+G, F, 0, Esc; cables with hover info, moving dots only when the source is active; breadcrumbs inside groups, cables to modules outside drawn to the edge |
| Quick face | Canvas.cpp NodeView | header (category tick, name, VOICE/GLOBAL/NOTES chip, power LED = bypass), typed ports with names, one mode menu + up to four knobs (only those that apply), live strip |
| Live strips | Canvas.cpp paintTelemetry | oscillator Hz, arp / sequencer steps, grain dots + position, recording waveform + playhead + status (LOADING / PLAYABLE, ANALYSING / MISSING), morph partials of both sides, satellite position, voices, host status, meters |
| Quick Add | QuickAdd.* | at the cursor; fuzzy search; recent + categories; CPU tag for heavy modules; filtered to compatible modules from a port; Return / Esc / arrows |
| Detail panel | DetailPanel.* | title, mode, help; labelled modes; controls (progressive disclosure); recording / plugin sections; 16-step editor; incoming modulation with depth sliders |
| ParamKnob | Widgets.* | drag, Shift fine, double-click default, wheel; right-click: reset, type a value, assign to macro 1–8, automate from a host slot 1–32, modulate from…, remove modulation; ember ring shows the live value |
| Toast | Widgets.* | what an action did, with UNDO |
| PLAY | PlayView.* | instrument name and description, live output trace, eight macros (one row or two), scenes I–IV + STORE + scene fader, EVOLVE / SAVE / PATCH |
| MacroKnob | PlayView.* | host parameter attachment; colour per slot; menu: rename, colour, default, each mapping's range / curve / remove |
| EvolvePanel | PlayView.* | SUBTLE / MODERATE / WILD, locks (scale, rhythm, routing, levels), seed + NEW SEED, EVOLVE, A / B, KEEP, list of changes |

Screenshots of the reference states at 869 / 1448 / 1810: `docs/screenshots/` (regenerate with
`MODULAR_SCREENSHOT_DIR=dir modular_product_tests "[screens]"`).
