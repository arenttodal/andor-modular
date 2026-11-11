#pragma once

#include "graph/Document.h"
#include "graph/Registry.h"

#include <string>
#include <vector>

namespace andor::modular
{

/** Prototype budgets (docs/PERFORMANCE_BUDGET.md). Engineering limits, not measured
    capabilities: tools/modular-bench measures them and they can be tightened. */
struct Budgets
{
    int sources = 6;
    int nodes = 32;
    int connections = 64;
    int arps = 6;
    int morphs = 2;
    int modulations = 32;
    int externals = 1;
    int groupDepth = 2;          ///< top level + one nested level
    float costUnits = 160.0f;    ///< flattened cost x polyphony (see CostProfile)
};

struct Issue
{
    enum class Severity { info, warning, error };
    Severity severity = Severity::error;
    std::string code;            ///< stable: "cycle", "fan-in", "domain", "budget.sources", ...
    std::string message;         ///< for people
    std::string node, connection;
    std::string suggestion;      ///< a node type that would fix it ("util.voicemix")
};

struct ValidationReport
{
    std::vector<Issue> issues;
    std::vector<std::string> invalidConnections;   ///< skipped by the compiler
    int sources = 0, nodes = 0, connections = 0, arps = 0, morphs = 0, modulations = 0, externals = 0;
    float cost = 0.0f;
    bool hasErrors() const noexcept;
    std::string summary() const;
};

/** Why a connection is (not) allowed: used by the canvas before a cable is made, and by the
    validator for every saved connection. */
struct ConnectionCheck
{
    bool ok = false;
    std::string reason;          ///< "VOICE audio cannot reach GLOBAL nodes directly"
    std::string suggestion;      ///< converter node type
    bool implicitBroadcast = false;   ///< global control into a voice: every voice hears it
};

class Validator
{
public:
    explicit Validator (const Registry& r, Budgets b = {}) : registry (r), budgets (b) {}

    ValidationReport validate (const Document& doc) const;

    ConnectionCheck checkConnection (const Document& doc, const std::string& from, const std::string& fromPort, const std::string& to,
                                     const std::string& toPort, bool toParam, const std::string& ignoreConnection = {}) const;

    /** Whether adding this connection would make a cycle not broken by a FEEDBACK DELAY. */
    bool wouldCycle (const Document& doc, const std::string& from, const std::string& to) const;

    /** The node's effective domain for a port (BOUNDARY inputs are voice-side). */
    static Domain portDomain (const NodeModel& node, const NodeDesc& desc, const PortDesc& port, bool input) noexcept;

    const Budgets& limits() const noexcept { return budgets; }
    /** Polyphony-weighted cost of the patch (sources and voice nodes cost per voice). */
    float cost (const Document& doc) const;

private:
    const Registry& registry;
    Budgets budgets;
};

} // namespace andor::modular
