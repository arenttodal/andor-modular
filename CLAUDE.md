# CLAUDE.md — working rules for ANDOR MODULAR

ANDOR Modular is built on ANDOR OSP (`external/osp`) and ANDOR Analog (`external/analog`),
pinned submodules. Their rules apply here too: real-time safety, determinism, versioned state,
goldens updated only for intentional changes, `docs/STATUS.md` kept current.

## Build & test

```sh
git submodule update --init
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
cmake -B build-plugin -G Ninja -DMODULAR_BUILD_PLUGIN=ON && cmake --build build-plugin
xvfb-run -a ./build-plugin/product/modular_product_tests_artefacts/Release/modular_product_tests
```

Keep both green on every commit.

## Rules

1. **Never modify the submodules' code from here.** A change Modular needs in OSP or Analog is
   made on a branch of that repository (with its own tests), then the pin moves here.
2. **The audio thread** (`Engine::process`, every `Processor::process/start/release/...`) never
   allocates, frees, locks, logs, reads files or waits. Allocate in `prepare()` / the compiler.
   `tests/RealtimeTests.cpp` counts heap calls; it must stay at zero.
3. **Determinism.** Random choices come from `osp::Prng` seeded from the patch seed, the node
   id and event counters. Output is identical for any host block size (the 32-sample grid).
4. **Stable identities.** Node type ids, parameter ids, port ids and host parameter ids are
   saved: never rename; add new ones. The patch schema is versioned (`Document::schemaVersion`)
   and every change needs a migration in `Document.cpp` and a fixture test.
5. **Factory content is generated** by `dsp/edit/Factory.cpp` (`modular-render --write-factory
   presets/factory`, then move `modules/` to `presets/modules`). `FactoryTests` checks the files
   match the builder: regenerate deliberately, explain why in the commit.
6. **No fake controls.** A module or parameter that does nothing is not offered; an unavailable
   one says why (`NodeDesc::available`, `unavailableReason`).
7. **Every smart action is one undoable transaction** that reports what it did.
8. Features shared with OSP/Analog (SPACE, ECHO, ARP) come from their shared code; Modular
   wraps, it does not fork.
9. **The interface is the ANDOR interface** (`external/osp/andor-design-system/guide`), composed
   from the family's code and rendered assets in the submodules (docs/UI_COMPONENTS.md). No new
   colour, font, knob, key, texture or radius is defined here: `product/Source/Theme.h` names
   and maps what exists. Lay out in reference pixels (1448 x 1086, scaled as one piece). After a
   visual change, regenerate `docs/screenshots` and look at 869 / 1448 / 1810.

## Conventions

C++20, JUCE style (4-space indent, space before call parentheses, camelCase functions,
PascalCase types). Comments say why. Units in names or docs (seconds, Hz, dB, st, cents).
