#pragma once

#include "graph/Node.h"

#include <map>
#include <string>
#include <vector>

namespace andor::modular
{

/** Every node type this build knows (the built-in ANDOR modules). Immutable after creation:
    shared by the message thread, the compiler and the tools. */
class Registry
{
public:
    /** The built-in catalogue (dsp/nodes). */
    static const Registry& builtIn();

    void add (NodeDesc desc);
    const NodeDesc* find (const std::string& type) const noexcept;
    const std::vector<const NodeDesc*>& all() const noexcept { return ordered; }

    /** Fuzzy search for Quick Add: name, type, keywords, category, description (ranked). */
    std::vector<const NodeDesc*> search (const std::string& query, int maxResults = 24) const;

private:
    std::map<std::string, NodeDesc> types;
    std::vector<const NodeDesc*> ordered;
};

// dsp/nodes registration (one function per file).
void registerEventNodes (Registry&);
void registerSourceNodes (Registry&);
void registerProcessorNodes (Registry&);
void registerControlNodes (Registry&);
void registerSpatialNodes (Registry&);
void registerUtilityNodes (Registry&);
void registerFxNodes (Registry&);

/** Fuzzy match score (0: no match). Subsequence matching with bonuses for word starts. */
int fuzzyScore (const std::string& query, const std::string& candidate) noexcept;

} // namespace andor::modular
