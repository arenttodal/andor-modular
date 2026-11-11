#!/usr/bin/env bash
# Renders the factory patches as listening demos (spec §11). Deterministic: the same build
# renders the same samples. Usage: tools/render-demos.sh BUILD_DIR OUT_DIR
set -euo pipefail
build="${1:-build}"
out="${2:-renders}"
render="$build/tools/modular-render"
here="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$out"
phrase="48:0:3.5:0.8,55:0:3.5:0.7,60:0.5:3:0.75,64:1:2.5:0.7,67:4:3.5:0.8,62:4:3.5:0.7,57:4.5:3:0.7"
for p in "$here"/presets/factory/*.andormodular; do
    name="$(basename "$p" .andormodular)"
    extra=()
    case "$name" in
        scene-performance) extra=(--scene-sweep 0:3) ;;     # Demo 7: stillness -> afterglow
        generative-sequence) phrase="60:0:8:0.8" ;;          # Demo 5: the generator plays by itself
    esac
    "$render" --patch "$p" --out "$out/$name.wav" --seconds 8 --notes "$phrase" "${extra[@]}" 2>/dev/null
    phrase="48:0:3.5:0.8,55:0:3.5:0.7,60:0.5:3:0.75,64:1:2.5:0.7,67:4:3.5:0.8,62:4:3.5:0.7,57:4.5:3:0.7"
done
# Demo 5: the same seed renders the same notes, sample for sample.
"$render" --patch "$here/presets/factory/generative-sequence.andormodular" --out "$out/generative-again.wav" --seconds 8 --notes "60:0:8:0.8" >/dev/null 2>&1
cmp -s "$out/generative-sequence.wav" "$out/generative-again.wav" && echo "generative: identical re-render (same seed)"
# Demo 1 / determinism across host block sizes.
"$render" --patch "$here/presets/factory/basic-poly.andormodular" --out "$out/basic-poly-block37.wav" --seconds 8 --block 37 --notes "$phrase" >/dev/null 2>&1
cmp -s "$out/basic-poly.wav" "$out/basic-poly-block37.wav" && echo "basic poly: identical at host block 37 and 256"
