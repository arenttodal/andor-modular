# Saved state (`andormodular` schema 2)

A patch is deterministic JSON (`dsp/graph/Document.cpp`), keys in a fixed order, parameter maps
sorted; saved atomically (`file.tmp` then rename).

```jsonc
{
  "format": "andormodular", "schemaVersion": 2,
  "name": "...", "author": "...", "description": "...",
  "settings": { "polyphony": 8, "voiceMode": 0, "glide": 0, "seed": "1", "masterDb": 0, "bendRange": 2, "limiter": true },
  "nodes": [ { "id": "n-0001", "type": "osc.analog", "version": 1, "domain": "voice", "x": 0, "y": 0, "size": 1,
               "enabled": true, "label": "", "group": "", "params": { "...": 0.5 }, "strings": { "sample": "samples/x.wav" } } ],
  "connections": [ { "id": "c-0001", "from": "n-0002", "fromPort": "notes", "to": "n-0001", "toPort": "notes",
                     "toParam": false, "depth": 1, "transpose": 0 } ],
  "groups": [ { "id": "g-0001", "name": "BOWED MACHINE", "parent": "", "x": 0, "y": 0, "collapsed": true, "module": "",
                "ports": [ { "name": "NOTES", "node": "n-0001", "port": "notes", "input": true } ],
                "controls": [ /* macros: name, value, default, color, smooth, mappings */ ] } ],
  "macros": [ { "name": "CUTOFF", "value": 0.4, "default": 0.4, "color": 0, "smooth": 0.03,
                "mappings": [ { "node": "n-0003", "param": "cutoff", "min": 0.25, "max": 0.85, "curve": "linear" } ] } ],
  "scenes": [ { "name": "STILLNESS", "captured": true, "values": { "n-0003/cutoff": 900 }, "macros": [0.35, 0, ...] } ],
  "scenePosition": 0, "currentScene": 0,
  "hostBindings": [ { "slot": 3, "node": "n-0003", "param": "cutoff" } ],
  "view": { "zoom": 1, "panX": 0, "panY": 0, "group": "", "play": false, "panelLayout": 0, "visualisation": "scope" },
  "evolve": { "seed": "1", "generation": 0 },
  "idCounter": "12",
  "dependencies": [ "samples/bowed-c3.wav" ]
}
```

Rules:
- A node and a group may carry `"muted": true` and `"solo": true` (written only when set, so
  files without them are unchanged and older versions read them as before). They are the
  musician's mix state, separate from `enabled` (bypass): `audibleGain` (Document.h) fades a
  source at its own output; mute wins over solo; a group's reach the sources inside it.
- A sound source may carry `"modes"`: its other TYPES as it left them, by module type id
  (written only when any; files without it are unchanged):
  `"modes": { "src.organic": { "domain": "voice", "params": {…}, "strings": { "sample": "…" },
  "cables": [ { "from", "fromPort", "to", "toPort", "toParam", "depth", "transpose" } ],
  "mappings": [ { "slot", "host", "param", "min", "max", "curve" } ] } }`.
  `cables` and `mappings` are what only that type could use (set aside, restored when the
  source is that type again, or taken back by a type that can use them). The node's own
  `type`, `params` and `strings` are always the active type's (`edit::changeSourceType`).
- Parameters added on 2026-11-12, each at the shared processor's default (older patches play
  as they did): ECHO `interval`, `motion`; SPACE `resonance`, `freeze`, `abyssVariant`,
  `vintageVariant`, `vintageEra`, `vintageDiffusion`, `bloomVariant`, `bloomSync`,
  `bloomDivision`, `bloomTime`, `bloomDiffusion` and TYPE's appended VINTAGE, BLOOM; MOVEMENT
  `pattern`, `shaperRate`, `target`, `smooth`, `custom`, `s1`…`s16`, `e1`…`e16`, `h1`…`h16`
  and MODE's appended SHAPER. ECHO's DIVISION names were corrected (the stored index and the
  sound did not change).
- Loading runs the layout rule once (`edit::resolveOverlaps`): a patch saved with overlapping
  tiles opens with them apart. Positions are the only thing it changes.
- Ids are stable; a group control is addressed as `{ "node": <group id>, "param": "control.N" }`.
- Unknown module types load as **missing modules**, kept with their parameters and cables,
  silent; unknown parameters of a known type are kept (a newer version's state survives).
- Values are clamped to each parameter's range on load; NaN/inf become the default.
- `seed` and `idCounter` are strings (64-bit safe).
- Recording keys are relative to the patch's folder (or the factory folder) or absolute; a
  missing recording is reported and kept until relinked (drop a file on its module).
- EXTERNAL INSTRUMENT: `strings.plugin` (file), `strings.state` (base64 plugin state).

**Migration.** Schema 0 (the kernel's pre-release form: `edges` with `src`/`dst` "node:port",
`pos: [x, y]`) migrates to 1 (`tests/GraphTests.cpp`). A newer schema is refused, never misread.

**Schema 2 (2026-11-12): the effect modules carry ANDOR Analog's whole FX library table.**
`fx.phaser`, `fx.flanger`, `fx.shifter`, `fx.compressor`, `fx.filter`, `fx.retro`, `fx.spread`
and `fx.convolution` keep every parameter they had (id, unit, order) and gain one parameter for
each further setting of the effect, named as the library names it (`knee`, `fet.input`,
`multi.depth`, `wow.depth`, `sync`, `division`…; `dsp/nodes/AnalogFx.h`, the table is Analog's
`dsp/fx/FxParameterTable`). Ten existing parameters grew to the library's range:

| Module | Parameter | Schema 1 | Schema 2 |
|---|---|---|---|
| PHASER | `centre` | 100..5000 Hz | 80..8000 Hz |
| PHASER | `feedback` | 0..95 % | -85..95 % |
| FLANGER | `delay` | 0.5..10 ms | 0.2..12 ms |
| COMPRESSOR | `mode` | CLEAN, FET | CLEAN, FET, MULTIBAND |
| COMPRESSOR | `threshold` | -48..0 dB | -60..0 dB |
| COMPRESSOR | `makeup` | 0..24 dB | -12..24 dB |
| CONVOLUTION | `predelay` | 0..200 ms | 0..250 ms |
| CONVOLUTION | `width` | 0..100 % | 0..200 % |
| CONVOLUTION | `lowcut` | 20..1000 Hz | 20..2000 Hz |
| CONVOLUTION | `decay` | 0.2..8 s | 0.1..20 s |

Migration 1 → 2 (`Document.cpp`, `migrateV1`; fixture `tests/fixtures/patch-v1-fx.json`,
`[state][fx]`):
- values are saved in their own units and are untouched;
- what is saved normalised over a parameter's range (a macro's or a group control's mapping,
  a modulation cable's depth) is moved to the new range, so it reaches exactly the values it
  did;
- COMPRESSOR's FET input used to follow THRESHOLD live; `fet.input` starts at the patch's
  value, and whatever moved THRESHOLD (a macro or group control mapping, a modulation cable, a
  scene) gets a companion on `fet.input` moving it the same way;
- a new setting left at its default is not written to the processor, which then plays the
  library's built-in value: a schema 1 patch renders bit for bit as before (measured: eight
  modules at default and non-default settings, identical samples; the migrated fixture with
  macros and modulation within 5e-6 of full scale, float rounding of the moved ranges).

`fx.eq` (EQUALIZER) and `fx.utility` (UTILITY) were added after the table: every parameter
of theirs is the table's own name (`band2.gain`, `hp.freq`, `monoBass`…). New module types
need no migration; a build from before them keeps such a module as a missing module.

One thing is not kept: a modulation deep enough to push one of the ten parameters against the
end of its old range now carries on to the new end.

**Modules** (`.andormodule`): the same JSON with `"format": "andormodule"`, `"module"` (name),
`"root"` (the group id), `"hash"` (FNV-1a of its nodes and cables) and only the group's nodes,
nested groups and inner cables. Importing gives fresh ids and its own runtime.

**Plugin state** (host project): `ANDOR_MODULAR_STATE` ValueTree (XML in binary) with the
session (patch JSON + file path) and the parameter bank; on restore the host's parameter
values win over the patch's stored macro positions.
