#include "graph/Validator.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace andor::modular
{

bool ValidationReport::hasErrors() const noexcept
{
    for (const auto& i : issues)
        if (i.severity == Issue::Severity::error)
            return true;
    return false;
}

std::string ValidationReport::summary() const
{
    std::ostringstream s;
    s << "sources " << sources << ", nodes " << nodes << ", connections " << connections << ", arps " << arps << ", morphs " << morphs
      << ", modulations " << modulations << ", cost " << cost;
    for (const auto& i : issues)
        s << "\n  [" << (i.severity == Issue::Severity::error ? "error" : (i.severity == Issue::Severity::warning ? "warning" : "info"))
          << "] " << i.code << ": " << i.message;
    return s.str();
}

Domain Validator::portDomain (const NodeModel& node, const NodeDesc& desc, const PortDesc& port, bool input) noexcept
{
    if (port.kind == PortKind::notes)
        return Domain::event;
    if (desc.role == NodeRole::boundary)
        return input && port.voiceSide ? Domain::voice : Domain::global;
    if (node.domain == Domain::event)
        return Domain::global;   // an event node's control outputs (clock, step lanes) are global signals
    return node.domain;
}

float Validator::cost (const Document& doc) const
{
    float total = 0.0f;
    for (const auto& n : doc.nodes)
    {
        const auto* d = registry.find (n.type);
        if (d == nullptr || ! n.enabled)
            continue;
        total += d->cost.units * (n.domain == Domain::voice ? static_cast<float> (doc.settings.polyphony) : 1.0f);
    }
    return total;
}

bool Validator::wouldCycle (const Document& doc, const std::string& from, const std::string& to) const
{
    if (from == to)
        return true;
    auto isFeedback = [&] (const std::string& id) {
        const auto* n = doc.findNode (id);
        const auto* d = n != nullptr ? registry.find (n->type) : nullptr;
        return d != nullptr && d->role == NodeRole::feedback;
    };
    if (isFeedback (to))
        return false;   // its input is read a chunk later: never a zero-delay loop
    // Can `from` be reached from `to`? (Edges into a FEEDBACK DELAY do not continue the path.)
    std::set<std::string> seen;
    std::vector<std::string> stack { to };
    while (! stack.empty())
    {
        const auto id = stack.back();
        stack.pop_back();
        if (id == from)
            return true;
        if (! seen.insert (id).second)
            continue;
        for (const auto& c : doc.connections)
            if (c.from == id && ! isFeedback (c.to))
                stack.push_back (c.to);
    }
    return false;
}

ConnectionCheck Validator::checkConnection (const Document& doc, const std::string& from, const std::string& fromPort, const std::string& to,
                                            const std::string& toPort, bool toParam, const std::string& ignoreConnection) const
{
    ConnectionCheck r;
    const auto* a = doc.findNode (from);
    const auto* b = doc.findNode (to);
    if (a == nullptr || b == nullptr)
    {
        r.reason = "one end of the cable is not in the patch";
        return r;
    }
    if (from == to)
    {
        r.reason = "a module cannot feed itself (use FEEDBACK DELAY)";
        r.suggestion = "util.feedback";
        return r;
    }
    const auto* da = registry.find (a->type);
    const auto* db = registry.find (b->type);
    if (da == nullptr || db == nullptr)
    {
        r.reason = "a missing module cannot be connected";
        return r;
    }
    if (da->role == NodeRole::output)
    {
        r.reason = "the OUTPUT goes to the host (and the safety limiter)";
        return r;
    }
    const int oi = da->outputIndex (fromPort);
    if (oi < 0)
    {
        r.reason = da->name + " has no output '" + fromPort + "'";
        return r;
    }
    auto out = da->outputs[static_cast<std::size_t> (oi)];
    out.kind = effectiveKind (out, a->domain);
    const Domain fromDomain = portDomain (*a, *da, out, false);

    if (toParam)
    {
        const int pi = db->paramIndex (toPort);
        if (pi < 0)
        {
            r.reason = db->name + " has no parameter '" + toPort + "'";
            return r;
        }
        if (! db->params[static_cast<std::size_t> (pi)].modulatable)
        {
            r.reason = db->params[static_cast<std::size_t> (pi)].name + " cannot be modulated (it changes the structure)";
            return r;
        }
        if (! isControlLike (out.kind))
        {
            r.reason = "audio cannot modulate a parameter directly: follow its level first";
            r.suggestion = "ctl.follower";
            return r;
        }
        const Domain toDomain = b->domain == Domain::event ? Domain::global : b->domain;
        if (fromDomain == Domain::voice && toDomain == Domain::global)
        {
            r.reason = "a per-voice control cannot set a global parameter: combine the voices first";
            r.suggestion = "util.polyglobal";
            return r;
        }
        if (fromDomain == Domain::voice && toDomain == Domain::voice && wouldCycle (doc, from, to))
        {
            r.reason = "this modulation would make a loop";
            return r;
        }
        if (fromDomain == Domain::global && toDomain == Domain::voice)
            r.implicitBroadcast = true;
        if (wouldCycle (doc, from, to))
        {
            r.reason = "this modulation would make a loop (only FEEDBACK DELAY may close one)";
            r.suggestion = "util.feedback";
            return r;
        }
        r.ok = true;
        return r;
    }

    const int ii = db->inputIndex (toPort);
    if (ii < 0)
    {
        r.reason = db->name + " has no input '" + toPort + "'";
        return r;
    }
    auto in = db->inputs[static_cast<std::size_t> (ii)];
    in.kind = effectiveKind (in, b->domain);
    const Domain toDomain = portDomain (*b, *db, in, true);

    // Kinds.
    if ((out.kind == PortKind::notes) != (in.kind == PortKind::notes))
    {
        r.reason = out.kind == PortKind::notes ? "notes go into a note input (the small arrow port)" : "only notes can enter a note input";
        return r;
    }
    if (isAudio (in.kind) && ! isAudio (out.kind) && ! in.acceptsControl)
    {
        r.reason = "a control signal is not audio here: route it to a parameter instead";
        return r;
    }
    if (isAudio (out.kind) && ! isAudio (in.kind) && in.kind != PortKind::notes)
    {
        r.reason = "audio into a control input: follow its level first";
        r.suggestion = "ctl.follower";
        return r;
    }
    if (out.kind == PortKind::audioStereo && in.kind == PortKind::audioMono)
    {
        r.reason = "stereo into a mono input: split or sum it first";
        r.suggestion = "util.split";
        return r;
    }

    // Domains.
    if (out.kind != PortKind::notes)
    {
        if (fromDomain == Domain::voice && toDomain == Domain::global)
        {
            r.reason = isAudio (out.kind) ? "voice audio reaches global modules through VOICE MIX" : "per-voice controls reach global modules through POLY > GLOBAL";
            r.suggestion = isAudio (out.kind) ? "util.voicemix" : "util.polyglobal";
            return r;
        }
        if (fromDomain == Domain::global && toDomain == Domain::voice)
        {
            if (isAudio (out.kind))
            {
                r.reason = "global audio cannot enter a voice (each voice has its own sound)";
                return r;
            }
            r.implicitBroadcast = true;
        }
    }

    // Fan-in.
    if (! in.multi)
        for (const auto& c : doc.connections)
            if (c.id != ignoreConnection && c.to == to && ! c.toParam && c.toPort == toPort)
            {
                r.reason = "this input already has a cable: mix them first";
                r.suggestion = isAudio (in.kind) ? (toDomain == Domain::voice ? "util.voicemix" : "util.mixer") : (in.kind == PortKind::notes ? "event.merge" : "ctl.math");
                return r;
            }

    for (const auto& c : doc.connections)
        if (c.id != ignoreConnection && c.from == from && c.fromPort == fromPort && c.to == to && c.toPort == toPort && ! c.toParam)
        {
            r.reason = "these two ports are already connected";
            return r;
        }

    if (wouldCycle (doc, from, to))
    {
        r.reason = "this would make a loop (only FEEDBACK DELAY may close one)";
        r.suggestion = "util.feedback";
        return r;
    }
    r.ok = true;
    return r;
}

ValidationReport Validator::validate (const Document& doc) const
{
    ValidationReport rep;
    auto issue = [&] (Issue::Severity s, std::string code, std::string msg, std::string node = {}, std::string conn = {}, std::string suggestion = {}) {
        rep.issues.push_back ({ s, std::move (code), std::move (msg), std::move (node), std::move (conn), std::move (suggestion) });
    };

    int outputs = 0;
    std::map<std::string, int> groupDepth;
    std::function<int (const std::string&, int)> depthOf = [&] (const std::string& gid, int guard) -> int {
        if (gid.empty() || guard > 16)
            return 0;
        const auto* g = doc.findGroup (gid);
        return g == nullptr ? 0 : 1 + depthOf (g->parent, guard + 1);
    };

    for (const auto& n : doc.nodes)
    {
        const auto* d = registry.find (n.type);
        if (d == nullptr)
        {
            issue (Issue::Severity::warning, "missing-module", "unknown module '" + n.type + "' is kept silent with its settings", n.id);
            continue;
        }
        if (! d->supports (n.domain))
            issue (Issue::Severity::error, "domain", d->name + " cannot run in the " + std::string (domainName (n.domain)) + " domain", n.id);
        if (! d->available)
            issue (Issue::Severity::warning, "unavailable", d->name + ": " + d->unavailableReason, n.id);
        ++rep.nodes;
        if (! n.enabled)
            continue;
        if (d->role == NodeRole::source)
            ++rep.sources;
        if (d->role == NodeRole::output)
            ++outputs;
        if (d->isArp)
            ++rep.arps;
        if (d->isMorph)
            ++rep.morphs;
        if (d->isExternal)
            ++rep.externals;
        if (depthOf (n.group, 0) + 1 > budgets.groupDepth)
            issue (Issue::Severity::warning, "group-depth", "groups nest one level deep in this version", n.id);
        for (const auto& key : d->strings)
            if (key == "sample")
            {
                const auto it = n.strings.find (key);
                if (it == n.strings.end() || it->second.empty())
                    issue (Issue::Severity::info, "no-sample", d->name + " is waiting for a recording (drop one on it)", n.id);
            }
    }
    rep.cost = cost (doc);

    if (outputs == 0)
        issue (Issue::Severity::warning, "no-output", "the patch has no OUTPUT: it is silent", {}, {}, "util.output");
    if (outputs > 1)
        issue (Issue::Severity::error, "outputs", "only one OUTPUT may sound");

    // Connections, in order (the first cable into a single input wins).
    std::set<std::string> used;   // "node/port" of single inputs taken
    for (const auto& c : doc.connections)
    {
        const auto* a = doc.findNode (c.from);
        const auto* b = doc.findNode (c.to);
        if (a == nullptr || b == nullptr || registry.find (a->type) == nullptr || registry.find (b->type) == nullptr)
        {
            rep.invalidConnections.push_back (c.id);
            continue;
        }
        if (! a->enabled && ! b->enabled)
        {
            // kept (disabled modules keep their cables)
        }
        ++rep.connections;
        if (c.toParam)
            ++rep.modulations;

        // Types and domains (fan-in and cycles are checked below, globally).
        const auto check = checkConnection (doc, c.from, c.fromPort, c.to, c.toPort, c.toParam, c.id);
        bool bad = ! check.ok;
        std::string reason = check.reason;
        // checkConnection reports fan-in against *any* other cable; keep the first one.
        if (bad && reason.rfind ("this input already has a cable", 0) == 0)
        {
            const auto key = c.to + "/" + c.toPort;
            bad = used.count (key) > 0;
        }
        if (! c.toParam)
        {
            const auto* db = registry.find (b->type);
            const int ii = db->inputIndex (c.toPort);
            if (ii >= 0 && ! db->inputs[static_cast<std::size_t> (ii)].multi)
            {
                const auto key = c.to + "/" + c.toPort;
                if (used.count (key) > 0)
                {
                    bad = true;
                    reason = "a second cable into a single input";
                }
                used.insert (key);
            }
        }
        if (bad)
        {
            rep.invalidConnections.push_back (c.id);
            issue (Issue::Severity::error, "connection", reason, c.to, c.id, check.suggestion);
        }
    }

    // Budgets.
    auto budget = [&] (int value, int limit, const char* code, const char* what) {
        if (value > limit)
            issue (Issue::Severity::error, code, std::string ("more than ") + std::to_string (limit) + " " + what + " (" + std::to_string (value) + ")");
    };
    budget (rep.sources, budgets.sources, "budget.sources", "sound sources");
    budget (rep.nodes, budgets.nodes, "budget.nodes", "modules");
    budget (rep.connections, budgets.connections, "budget.connections", "connections");
    budget (rep.arps, budgets.arps, "budget.arps", "arpeggiators");
    budget (rep.morphs, budgets.morphs, "budget.morphs", "MORPH modules");
    budget (rep.modulations, budgets.modulations, "budget.modulations", "modulation routes");
    budget (rep.externals, budgets.externals, "budget.external", "external instruments");
    if (rep.cost > budgets.costUnits)
        issue (Issue::Severity::warning, "budget.cpu", "this patch may exceed the CPU budget: lower the polyphony or use fewer heavy modules");

    // Global cycle check (Kahn), feedback inputs excluded.
    {
        std::map<std::string, int> indeg;
        for (const auto& n : doc.nodes)
            indeg[n.id] = 0;
        auto skip = [&] (const Connection& c) {
            if (std::find (rep.invalidConnections.begin(), rep.invalidConnections.end(), c.id) != rep.invalidConnections.end())
                return true;
            const auto* b = doc.findNode (c.to);
            const auto* db = b != nullptr ? registry.find (b->type) : nullptr;
            return db == nullptr || db->role == NodeRole::feedback;
        };
        for (const auto& c : doc.connections)
            if (! skip (c))
                ++indeg[c.to];
        std::vector<std::string> ready;
        for (const auto& [id, deg] : indeg)
            if (deg == 0)
                ready.push_back (id);
        std::size_t done = 0;
        while (! ready.empty())
        {
            const auto id = ready.back();
            ready.pop_back();
            ++done;
            for (const auto& c : doc.connections)
                if (c.from == id && ! skip (c) && --indeg[c.to] == 0)
                    ready.push_back (c.to);
        }
        if (done < indeg.size())
            for (const auto& c : doc.connections)
                if (! skip (c) && indeg[c.to] > 0 && indeg[c.from] > 0)
                {
                    issue (Issue::Severity::error, "cycle", "a loop without FEEDBACK DELAY", c.to, c.id, "util.feedback");
                    rep.invalidConnections.push_back (c.id);
                }
    }
    return rep;
}

} // namespace andor::modular
