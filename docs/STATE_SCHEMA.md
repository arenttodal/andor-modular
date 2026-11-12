# Saved state (`andormodular` schema 1)

A patch is deterministic JSON (`dsp/graph/Document.cpp`), keys in a fixed order, parameter maps
sorted; saved atomically (`file.tmp` then rename).

```jsonc
{
  "format": "andormodular", "schemaVersion": 1,
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

**Modules** (`.andormodule`): the same JSON with `"format": "andormodule"`, `"module"` (name),
`"root"` (the group id), `"hash"` (FNV-1a of its nodes and cables) and only the group's nodes,
nested groups and inner cables. Importing gives fresh ids and its own runtime.

**Plugin state** (host project): `ANDOR_MODULAR_STATE` ValueTree (XML in binary) with the
session (patch JSON + file path) and the parameter bank; on restore the host's parameter
values win over the patch's stored macro positions.
