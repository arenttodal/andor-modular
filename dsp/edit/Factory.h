#pragma once

#include "graph/Document.h"
#include "graph/Registry.h"

#include <string>
#include <vector>

/** The factory patches and modules, built with the same edit operations a musician uses
    (dogfooding the smart wiring). tools/modular-render --write-factory writes them to
    presets/; tests render every one. Recordings they use are ANDOR's own, synthesised by
    tools/make-samples (no third-party library audio). */
namespace andor::modular::factory
{

std::vector<Document> patches (const Registry& reg);

struct Module
{
    std::string name;
    std::string text;   ///< .andormodule JSON
};
std::vector<Module> modules (const Registry& reg);

/** Relative keys of the factory recordings (presets/factory/samples/...). */
constexpr const char* bowedSample = "samples/bowed-c3.wav";
constexpr const char* clockSample = "samples/clockwork-120.wav";
constexpr const char* rainSample = "samples/rain-texture.wav";

} // namespace andor::modular::factory
