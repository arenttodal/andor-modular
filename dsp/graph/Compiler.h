#pragma once

#include "graph/CompiledGraph.h"
#include "graph/External.h"

#include <memory>

namespace andor::modular
{

struct CompileOptions
{
    double sampleRate = 48000.0;
    int maxBlock = 512;
    AssetResolver assets;        ///< resolves a node's "sample" key to its loaded asset (may be empty)
    Budgets budgets;
    /** Hosted instruments (the plugin's ExternalHost); empty: the global factory. */
    std::function<std::shared_ptr<ExternalInstrument> (const std::string& nodeId, const std::vector<std::string>& strings)> externals;
};

/**
    Document -> CompiledGraph. Allocates and prepares everything; call it on the message thread
    or a background thread, never on the audio thread. Always returns a graph: invalid
    connections are reported (graph->report) and left out, missing modules stay silent, so a
    damaged patch still opens and plays what it can.
*/
std::unique_ptr<CompiledGraph> compileGraph (const Document& doc, const Registry& registry, const CompileOptions& options);

} // namespace andor::modular
