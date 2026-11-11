#include "graph/Compiler.h"

#include "core/Prng.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>

namespace andor::modular
{

namespace
{
    std::atomic<std::uint64_t> generationCounter { 0 };

    std::uint64_t hashString (const std::string& s) noexcept
    {
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (const char c : s)
        {
            h ^= static_cast<unsigned char> (c);
            h *= 0x100000001b3ull;
        }
        return h;
    }

    struct UnionFind
    {
        std::vector<int> parent;
        explicit UnionFind (int n) : parent (static_cast<std::size_t> (n)) { std::iota (parent.begin(), parent.end(), 0); }
        int find (int x)
        {
            while (parent[static_cast<std::size_t> (x)] != x)
                x = parent[static_cast<std::size_t> (x)] = parent[static_cast<std::size_t> (parent[static_cast<std::size_t> (x)])];
            return x;
        }
        void unite (int a, int b) { parent[static_cast<std::size_t> (find (a))] = find (b); }
    };

    /** Stable Kahn sort: among ready items the lowest index goes first (document order). */
    std::vector<int> topoSort (int count, const std::vector<std::pair<int, int>>& edges, bool& cyclic)
    {
        std::vector<int> indeg (static_cast<std::size_t> (count), 0);
        std::vector<std::vector<int>> out (static_cast<std::size_t> (count));
        std::set<std::pair<int, int>> unique (edges.begin(), edges.end());
        for (const auto& [a, b] : unique)
            if (a != b)
            {
                out[static_cast<std::size_t> (a)].push_back (b);
                ++indeg[static_cast<std::size_t> (b)];
            }
        std::set<int> ready;
        for (int i = 0; i < count; ++i)
            if (indeg[static_cast<std::size_t> (i)] == 0)
                ready.insert (i);
        std::vector<int> order;
        while (! ready.empty())
        {
            const int i = *ready.begin();
            ready.erase (ready.begin());
            order.push_back (i);
            for (const int j : out[static_cast<std::size_t> (i)])
                if (--indeg[static_cast<std::size_t> (j)] == 0)
                    ready.insert (j);
        }
        cyclic = static_cast<int> (order.size()) != count;
        // Whatever a cycle left (it was reported by the validator) runs last, in order.
        for (int i = 0; i < count; ++i)
            if (std::find (order.begin(), order.end(), i) == order.end())
                order.push_back (i);
        return order;
    }
}

bool CompiledGraph::setParam (const std::string& node, const std::string& param, double native) noexcept
{
    const auto it = nodeIndexById.find (node);
    if (it == nodeIndexById.end())
        return false;
    auto& n = nodes[static_cast<std::size_t> (it->second)];
    if (n.desc == nullptr)
        return false;
    const int p = n.desc->paramIndex (param);
    if (p < 0)
        return false;
    n.base[static_cast<std::size_t> (p)].store (static_cast<float> (n.desc->params[static_cast<std::size_t> (p)].clampNative (native)),
                                                std::memory_order_relaxed);
    return true;
}

double CompiledGraph::getParam (const std::string& node, const std::string& param) const noexcept
{
    const auto it = nodeIndexById.find (node);
    if (it == nodeIndexById.end())
        return 0.0;
    const auto& n = nodes[static_cast<std::size_t> (it->second)];
    if (n.desc == nullptr)
        return 0.0;
    const int p = n.desc->paramIndex (param);
    return p < 0 ? 0.0 : n.base[static_cast<std::size_t> (p)].load (std::memory_order_relaxed);
}

double CompiledGraph::effectiveParam (const std::string& node, const std::string& param) const noexcept
{
    const auto it = nodeIndexById.find (node);
    if (it == nodeIndexById.end())
        return 0.0;
    const auto& n = nodes[static_cast<std::size_t> (it->second)];
    if (n.desc == nullptr)
        return 0.0;
    const int p = n.desc->paramIndex (param);
    return p < 0 ? 0.0 : n.shown[static_cast<std::size_t> (p)].load (std::memory_order_relaxed);
}

bool CompiledGraph::setChannel (int channel, float value) noexcept
{
    if (channel < 0 || channel >= static_cast<int> (channels.size()))
        return false;
    channels[static_cast<std::size_t> (channel)]->value.store (std::clamp (value, 0.0f, 1.0f), std::memory_order_relaxed);
    return true;
}

int CompiledGraph::channelIndex (const std::string& groupId, int control) const noexcept
{
    const auto it = groupChannelBase.find (groupId);
    return it == groupChannelBase.end() ? -1 : it->second + control;
}

std::unique_ptr<CompiledGraph> compileGraph (const Document& doc, const Registry& registry, const CompileOptions& options)
{
    const auto started = std::chrono::steady_clock::now();
    auto g = std::make_unique<CompiledGraph>();
    g->generation = ++generationCounter;
    g->sampleRate = options.sampleRate;
    g->maxBlock = std::max (1, options.maxBlock);
    g->settings = doc.settings;
    Validator validator (registry, options.budgets);
    g->report = validator.validate (doc);

    const int count = static_cast<int> (doc.nodes.size());
    g->nodes = std::vector<CompiledNode> (static_cast<std::size_t> (count));
    for (int i = 0; i < count; ++i)
    {
        const auto& m = doc.nodes[static_cast<std::size_t> (i)];
        auto& n = g->nodes[static_cast<std::size_t> (i)];
        n.id = m.id;
        n.desc = registry.find (m.type);
        n.domain = n.desc != nullptr && n.desc->role == NodeRole::event ? Domain::event : m.domain;
        n.enabled = m.enabled;
        g->nodeIndexById[m.id] = i;
        if (n.desc == nullptr)
            continue;
        const auto np = n.desc->params.size();
        n.base = std::make_unique<std::atomic<float>[]> (np);
        n.shown = std::make_unique<std::atomic<float>[]> (np);
        n.baseNative.assign (np, 0.0f);
        n.baseNorm.assign (np, 0.0f);
        n.macroMapped.assign (np, 0);
        for (std::size_t p = 0; p < np; ++p)
        {
            const auto& pd = n.desc->params[p];
            const auto v = static_cast<float> (pd.clampNative (m.param (pd.id, pd.def)));
            n.base[p].store (v, std::memory_order_relaxed);
            n.shown[p].store (v, std::memory_order_relaxed);
            n.baseNative[p] = v;
            n.baseNorm[p] = static_cast<float> (pd.normalise (v));
        }
        for (const auto& key : n.desc->strings)
        {
            const auto it = m.strings.find (key);
            n.strings.push_back (it != m.strings.end() ? it->second : std::string {});
        }
        if (n.desc->role == NodeRole::output && n.enabled && g->outputNode < 0)
            g->outputNode = i;
        if (n.desc->role == NodeRole::boundary)
            g->boundaryNodes.push_back (i);
        if (n.desc->role == NodeRole::feedback)
            g->feedbackNodes.push_back (i);
        if (n.desc->type == "event.keyboard")
            ++g->keyboardNodes;
    }

    // Valid connections (the validator's rejects and anything touching a missing module drop out).
    struct Edge
    {
        int a, b, outPort, inPort, param;
        float depth;
        int transpose;
    };
    std::vector<Edge> edges;
    const std::set<std::string> invalid (g->report.invalidConnections.begin(), g->report.invalidConnections.end());
    for (const auto& c : doc.connections)
    {
        if (invalid.count (c.id) > 0)
            continue;
        const int a = doc.nodeIndex (c.from), b = doc.nodeIndex (c.to);
        if (a < 0 || b < 0)
            continue;
        const auto* da = g->nodes[static_cast<std::size_t> (a)].desc;
        const auto* db = g->nodes[static_cast<std::size_t> (b)].desc;
        if (da == nullptr || db == nullptr)
            continue;
        Edge e { a, b, da->outputIndex (c.fromPort), c.toParam ? -1 : db->inputIndex (c.toPort), c.toParam ? db->paramIndex (c.toPort) : -1,
                 static_cast<float> (c.depth), c.transpose };
        if (e.outPort < 0 || (e.inPort < 0 && e.param < 0))
            continue;
        if (e.inPort >= maxPorts || e.outPort >= maxPorts)
            continue;
        edges.push_back (e);
    }
    auto nodeAt = [&] (int i) -> CompiledNode& { return g->nodes[static_cast<std::size_t> (i)]; };
    auto isVoice = [&] (int i) { return nodeAt (i).desc != nullptr && nodeAt (i).domain == Domain::voice; };
    auto isEvent = [&] (int i) { return nodeAt (i).desc != nullptr && nodeAt (i).domain == Domain::event; };
    auto outPortDesc = [&] (const Edge& e) -> const PortDesc& { return nodeAt (e.a).desc->outputs[static_cast<std::size_t> (e.outPort)]; };
    auto isNotesEdge = [&] (const Edge& e) { return outPortDesc (e).kind == PortKind::notes; };

    // Lanes: connected components of voice nodes over voice-to-voice edges.
    UnionFind uf (std::max (1, count));
    for (const auto& e : edges)
        if (isVoice (e.a) && isVoice (e.b) && ! isNotesEdge (e))
            uf.unite (e.a, e.b);
    // (Lanes hold atomics: the vector is made at its final size, never grown.)
    std::map<int, int> laneOfRoot;
    for (int i = 0; i < count; ++i)
        if (isVoice (i))
        {
            const int r = uf.find (i);
            if (laneOfRoot.count (r) == 0)
            {
                const int next = static_cast<int> (laneOfRoot.size());
                laneOfRoot[r] = next;
            }
            nodeAt (i).lane = laneOfRoot[r];
        }
    g->lanes = std::vector<Lane> (laneOfRoot.size());
    const int polyphony = doc.settings.voiceMode == 0 ? std::clamp (doc.settings.polyphony, 1, maxVoices) : 1;

    // Event order (notes edges between event nodes).
    {
        std::vector<int> eventNodes;
        for (int i = 0; i < count; ++i)
            if (isEvent (i))
                eventNodes.push_back (i);
        std::vector<std::pair<int, int>> ev;
        for (const auto& e : edges)
            if (isEvent (e.a) && isEvent (e.b) && isNotesEdge (e))
            {
                const auto ia = std::find (eventNodes.begin(), eventNodes.end(), e.a) - eventNodes.begin();
                const auto ib = std::find (eventNodes.begin(), eventNodes.end(), e.b) - eventNodes.begin();
                ev.emplace_back (static_cast<int> (ia), static_cast<int> (ib));
            }
        bool cyclic = false;
        for (const int k : topoSort (static_cast<int> (eventNodes.size()), ev, cyclic))
            g->eventOrder.push_back (eventNodes[static_cast<std::size_t> (k)]);
    }

    // Notes routing: into event nodes, lanes and global note consumers.
    for (const auto& e : edges)
    {
        if (! isNotesEdge (e) || ! isEvent (e.a))
            continue;
        auto& b = nodeAt (e.b);
        if (isEvent (e.b))
        {
            if (b.eventSources.size() < b.desc->inputs.size())
                b.eventSources.resize (b.desc->inputs.size());
            b.eventSources[static_cast<std::size_t> (e.inPort)].push_back ({ e.a, e.outPort, e.transpose });
        }
        else if (isVoice (e.b))
        {
            auto& lane = g->lanes[static_cast<std::size_t> (b.lane)];
            bool known = false;
            for (const auto& s : lane.noteSources)
                known = known || (s.node == e.a && s.transpose == e.transpose);
            if (! known)
                lane.noteSources.push_back ({ e.a, e.transpose, static_cast<std::uint32_t> (hashString (nodeAt (e.a).id) ^ static_cast<std::uint32_t> (e.transpose + 1000) * 2654435761u) });
        }
        else
        {
            b.takesNotes = true;
            if (b.eventSources.size() < b.desc->inputs.size())
                b.eventSources.resize (b.desc->inputs.size());
            b.eventSources[static_cast<std::size_t> (e.inPort)].push_back ({ e.a, e.outPort, e.transpose });
        }
    }

    // Lane-internal order, and the unit schedule.
    for (std::size_t l = 0; l < g->lanes.size(); ++l)
    {
        auto& lane = g->lanes[l];
        std::vector<int> members;
        for (int i = 0; i < count; ++i)
            if (nodeAt (i).lane == static_cast<int> (l))
                members.push_back (i);
        std::vector<std::pair<int, int>> le;
        for (const auto& e : edges)
            if (nodeAt (e.a).lane == static_cast<int> (l) && nodeAt (e.b).lane == static_cast<int> (l) && ! isNotesEdge (e))
            {
                const auto ia = std::find (members.begin(), members.end(), e.a) - members.begin();
                const auto ib = std::find (members.begin(), members.end(), e.b) - members.begin();
                le.emplace_back (static_cast<int> (ia), static_cast<int> (ib));
            }
        bool cyclic = false;
        std::string key;
        for (const int k : topoSort (static_cast<int> (members.size()), le, cyclic))
        {
            const int idx = members[static_cast<std::size_t> (k)];
            lane.nodes.push_back (idx);
            if (nodeAt (idx).desc->endsVoice && nodeAt (idx).enabled)
                lane.endsVoice.push_back (idx);
        }
        std::vector<std::string> ids;
        for (const int idx : members)
            ids.push_back (nodeAt (idx).id);
        std::sort (ids.begin(), ids.end());
        for (const auto& id : ids)
            key += id + ",";
        lane.key = key;
        lane.voices.resize (static_cast<std::size_t> (polyphony));
        for (int v = 0; v < polyphony; ++v)
            lane.voices[static_cast<std::size_t> (v)].info.voiceIndex = v;
    }
    {
        // Units: lanes first (0..L-1), then global nodes.
        std::vector<int> globals;
        for (int i = 0; i < count; ++i)
            if (nodeAt (i).desc != nullptr && nodeAt (i).domain == Domain::global)
                globals.push_back (i);
        const int laneCount = static_cast<int> (g->lanes.size());
        auto unitOf = [&] (int node) -> int {
            if (isVoice (node))
                return nodeAt (node).lane;
            const auto it = std::find (globals.begin(), globals.end(), node);
            return it == globals.end() ? -1 : laneCount + static_cast<int> (it - globals.begin());
        };
        std::vector<std::pair<int, int>> ue;
        for (const auto& e : edges)
        {
            if (isNotesEdge (e) || isEvent (e.a))
                continue;
            if (nodeAt (e.b).desc->role == NodeRole::feedback && e.param < 0)
                continue;   // read a chunk later
            const int ua = unitOf (e.a), ub = unitOf (e.b);
            if (ua >= 0 && ub >= 0 && ua != ub)
                ue.emplace_back (ua, ub);
        }
        bool cyclic = false;
        for (const int u : topoSort (laneCount + static_cast<int> (globals.size()), ue, cyclic))
            g->schedule.push_back (u < laneCount ? Unit { true, u } : Unit { false, globals[static_cast<std::size_t> (u - laneCount)] });
    }

    // Instances: shared state, processors, prepared.
    const std::uint64_t patchSeed = doc.settings.seed;
    for (int i = 0; i < count; ++i)
    {
        auto& n = nodeAt (i);
        if (n.desc == nullptr)
            continue;
        PrepareContext pc;
        pc.sampleRate = options.sampleRate;
        pc.maxBlock = g->maxBlock;
        pc.seed = osp::Prng::deriveSeed (patchSeed, hashString (n.id));
        pc.strings = &n.strings;
        pc.params = n.baseNative.empty() ? nullptr : n.baseNative.data();
        for (std::size_t s = 0; s < n.desc->strings.size(); ++s)
            if (n.desc->strings[s] == "sample" && ! n.strings[s].empty() && options.assets)
                pc.asset = options.assets (n.strings[s]);
        if (n.desc->createShared)
            n.shared = n.desc->createShared (pc);
        pc.shared = n.shared.get();
        if (n.domain == Domain::event)
        {
            if (n.desc->createEvent)
            {
                n.eventProcessor = n.desc->createEvent();
                n.eventProcessor->prepare (pc);
            }
            n.eventInputs.resize (n.desc->inputs.size());
            n.eventSources.resize (n.desc->inputs.size());
            n.eventControl.assign (n.desc->outputs.size(), std::vector<float> (static_cast<std::size_t> (g->maxBlock), 0.0f));
            n.instances.resize (1);   // chunk views of the control outputs
            n.instances[0].outputs.resize (n.desc->outputs.size());
            continue;
        }
        const int instanceCount = n.domain == Domain::voice ? polyphony : 1;
        n.instances.resize (static_cast<std::size_t> (instanceCount));
        for (int v = 0; v < instanceCount; ++v)
        {
            auto& inst = n.instances[static_cast<std::size_t> (v)];
            if (n.desc->create)
                inst.processor = n.desc->create();
            pc.voiceIndex = n.domain == Domain::voice ? v : -1;
            if (inst.processor != nullptr)
                inst.processor->prepare (pc);
            inst.outputs.resize (n.desc->outputs.size());
            inst.resolved.assign (n.desc->params.size(), 0.0f);
            for (std::size_t p = 0; p < n.desc->params.size(); ++p)
                inst.resolved[p] = n.baseNative[p];
        }
        n.notes.clear();
        if (n.takesNotes)
            n.eventSources.resize (n.desc->inputs.size());
    }

    // Latency (global path): each global node's output latency = its slowest input + its own.
    std::vector<int> latencyOf (static_cast<std::size_t> (count), 0);
    for (const auto& u : g->schedule)
    {
        if (u.lane)
            continue;
        auto& n = nodeAt (u.index);
        int in = 0;
        for (const auto& e : edges)
            if (e.b == u.index && e.param < 0 && ! isNotesEdge (e) && ! isVoice (e.a) && ! isEvent (e.a) && n.desc->role != NodeRole::feedback)
                in = std::max (in, latencyOf[static_cast<std::size_t> (e.a)]);
        const int own = ! n.instances.empty() && n.instances[0].processor != nullptr ? n.instances[0].processor->latency() : 0;
        latencyOf[static_cast<std::size_t> (u.index)] = in + std::max (own, n.desc->latencySamples);
        n.latency = latencyOf[static_cast<std::size_t> (u.index)];
    }
    if (g->outputNode >= 0)
        g->latencySamples = latencyOf[static_cast<std::size_t> (g->outputNode)];

    // Wiring.
    auto sourcePtr = [&] (const Edge& e, int voice) -> std::array<const float*, 2> {
        auto& a = nodeAt (e.a);
        const int v = a.domain == Domain::voice ? voice : 0;
        if (v < 0 || v >= static_cast<int> (a.instances.size()))
            return { g->zeros.ch[0].data(), g->zeros.ch[1].data() };
        auto& buf = a.instances[static_cast<std::size_t> (v)].outputs[static_cast<std::size_t> (e.outPort)];
        const bool stereo = effectiveKind (outPortDesc (e), a.domain) == PortKind::audioStereo;
        return { buf.ch[0].data(), stereo ? buf.ch[1].data() : buf.ch[0].data() };
    };

    for (int i = 0; i < count; ++i)
    {
        auto& n = nodeAt (i);
        if (n.desc == nullptr || n.domain == Domain::event)
            continue;
        const auto& d = *n.desc;
        for (std::size_t v = 0; v < n.instances.size(); ++v)
        {
            auto& inst = n.instances[v];
            const int voice = n.domain == Domain::voice ? static_cast<int> (v) : 0;
            // Count scratch needs first: buffers are never reallocated after pointers are taken.
            int scratchNeeded = 0;
            for (std::size_t p = 0; p < d.inputs.size() && p < static_cast<std::size_t> (maxPorts); ++p)
            {
                if (d.inputs[p].kind == PortKind::notes || (d.role == NodeRole::boundary && d.inputs[p].voiceSide))
                    continue;
                int sources = 0;
                for (const auto& e : edges)
                    if (e.b == i && e.inPort == static_cast<int> (p))
                    {
                        ++sources;
                        if (n.domain == Domain::global && ! isVoice (e.a) && ! isEvent (e.a) && latencyOf[static_cast<std::size_t> (e.a)] < n.latency - std::max (0, inst.processor != nullptr ? inst.processor->latency() : 0))
                            ++scratchNeeded;
                    }
                if (sources > 1)
                    ++scratchNeeded;
            }
            inst.scratch.resize (static_cast<std::size_t> (scratchNeeded));
            int scratchUsed = 0;
            const int ownLatency = std::max (inst.processor != nullptr ? inst.processor->latency() : 0, d.latencySamples);
            const int inputLatency = n.latency - ownLatency;

            for (std::size_t p = 0; p < static_cast<std::size_t> (maxPorts); ++p)
            {
                inst.io.in[p] = { g->zeros.ch[0].data(), g->zeros.ch[1].data() };
                inst.io.inConnected[p] = false;
            }
            for (std::size_t p = 0; p < d.inputs.size() && p < static_cast<std::size_t> (maxPorts); ++p)
            {
                if (d.inputs[p].kind == PortKind::notes || (d.role == NodeRole::boundary && d.inputs[p].voiceSide))
                    continue;
                std::vector<std::array<const float*, 2>> ptrs;
                for (const auto& e : edges)
                    if (e.b == i && e.inPort == static_cast<int> (p))
                    {
                        auto ptr = sourcePtr (e, voice);
                        if (isEvent (e.a))
                        {
                            auto& buf = nodeAt (e.a).instances[0].outputs[static_cast<std::size_t> (e.outPort)];
                            ptr = { buf.ch[0].data(), buf.ch[0].data() };
                        }
                        // Latency alignment of a shorter global path.
                        if (n.domain == Domain::global && ! isVoice (e.a) && ! isEvent (e.a))
                        {
                            const int delay = inputLatency - latencyOf[static_cast<std::size_t> (e.a)];
                            if (delay > 0 && scratchUsed < scratchNeeded)
                            {
                                AlignInput al;
                                al.port = static_cast<int> (p);
                                al.delay = delay;
                                al.channels = 2;
                                al.line.assign (static_cast<std::size_t> (2 * (delay + maxChunk + 1)), 0.0f);
                                al.out = &inst.scratch[static_cast<std::size_t> (scratchUsed++)];
                                al.source = ptr;
                                ptr = { al.out->ch[0].data(), al.out->ch[1].data() };
                                inst.aligns.push_back (std::move (al));
                            }
                        }
                        ptrs.push_back (ptr);
                    }
                if (ptrs.empty())
                    continue;
                inst.io.inConnected[p] = true;
                if (ptrs.size() == 1)
                    inst.io.in[p] = ptrs[0];
                else if (scratchUsed < scratchNeeded)
                {
                    SumInput s;
                    s.port = static_cast<int> (p);
                    s.channels = effectiveKind (d.inputs[p], n.domain) == PortKind::audioStereo ? 2 : 1;
                    s.sources = ptrs;
                    s.sum = &inst.scratch[static_cast<std::size_t> (scratchUsed++)];
                    inst.io.in[p] = { s.sum->ch[0].data(), s.channels == 2 ? s.sum->ch[1].data() : s.sum->ch[0].data() };
                    inst.sums.push_back (std::move (s));
                }
            }
            for (const auto& e : edges)
                if (e.b == i && e.param >= 0)
                {
                    std::array<const float*, 2> ptr = sourcePtr (e, voice);
                    if (isEvent (e.a))
                        ptr = { nodeAt (e.a).instances[0].outputs[static_cast<std::size_t> (e.outPort)].ch[0].data(), nullptr };
                    inst.mods.push_back ({ e.param, ptr[0], e.depth });
                }
            for (std::size_t q = 0; q < d.outputs.size() && q < static_cast<std::size_t> (maxPorts); ++q)
                inst.io.out[q] = { inst.outputs[q].ch[0].data(), inst.outputs[q].ch[1].data() };
            inst.io.params = inst.resolved.data();
            inst.io.strings = &n.strings;
        }
    }

    // Boundary feeds: per lane, per voice, the voice-side inputs.
    for (const int b : g->boundaryNodes)
    {
        auto& bn = nodeAt (b);
        const auto& d = *bn.desc;
        std::map<int, int> feedOfLane;
        for (const auto& e : edges)
        {
            if (e.b != b || e.inPort < 0 || ! d.inputs[static_cast<std::size_t> (e.inPort)].voiceSide || ! isVoice (e.a))
                continue;
            const int lane = nodeAt (e.a).lane;
            if (feedOfLane.count (lane) == 0)
            {
                feedOfLane[lane] = static_cast<int> (bn.feeds.size());
                BoundaryFeed f;
                f.lane = lane;
                f.voiceIO.resize (static_cast<std::size_t> (polyphony));
                f.voiceSums.resize (static_cast<std::size_t> (polyphony));
                f.voiceScratch.resize (static_cast<std::size_t> (polyphony));
                bn.feeds.push_back (std::move (f));
                g->lanes[static_cast<std::size_t> (lane)].boundaries.emplace_back (b, feedOfLane[lane]);
            }
        }
        for (auto& f : bn.feeds)
            for (int v = 0; v < polyphony; ++v)
            {
                auto& io = f.voiceIO[static_cast<std::size_t> (v)];
                for (std::size_t p = 0; p < static_cast<std::size_t> (maxPorts); ++p)
                {
                    io.in[p] = { g->zeros.ch[0].data(), g->zeros.ch[1].data() };
                    io.inConnected[p] = false;
                }
                // count multi-source ports
                int need = 0;
                for (std::size_t p = 0; p < d.inputs.size(); ++p)
                {
                    int sources = 0;
                    for (const auto& e : edges)
                        if (e.b == b && e.inPort == static_cast<int> (p) && isVoice (e.a) && nodeAt (e.a).lane == f.lane)
                            ++sources;
                    if (sources > 1)
                        ++need;
                }
                auto& scratch = f.voiceScratch[static_cast<std::size_t> (v)];
                scratch.resize (static_cast<std::size_t> (need));
                int used = 0;
                for (std::size_t p = 0; p < d.inputs.size() && p < static_cast<std::size_t> (maxPorts); ++p)
                {
                    std::vector<std::array<const float*, 2>> ptrs;
                    for (const auto& e : edges)
                        if (e.b == b && e.inPort == static_cast<int> (p) && isVoice (e.a) && nodeAt (e.a).lane == f.lane)
                            ptrs.push_back (sourcePtr (e, v));
                    if (ptrs.empty())
                        continue;
                    io.inConnected[p] = true;
                    if (ptrs.size() == 1)
                        io.in[p] = ptrs[0];
                    else
                    {
                        SumInput s;
                        s.port = static_cast<int> (p);
                        s.channels = 2;
                        s.sources = ptrs;
                        s.sum = &scratch[static_cast<std::size_t> (used++)];
                        io.in[p] = { s.sum->ch[0].data(), s.sum->ch[1].data() };
                        f.voiceSums[static_cast<std::size_t> (v)].push_back (std::move (s));
                    }
                }
                io.params = bn.instances[0].resolved.data();
                io.strings = &bn.strings;
            }
    }

    // Control channels: the eight macros, then every group's controls (outer groups first).
    for (int m = 0; m < maxMacros; ++m)
    {
        auto ch = std::make_unique<ControlChannel>();
        ch->name = doc.macros[static_cast<std::size_t> (m)].name;
        ch->value.store (static_cast<float> (doc.macros[static_cast<std::size_t> (m)].value), std::memory_order_relaxed);
        ch->smoothed = ch->effective = static_cast<float> (doc.macros[static_cast<std::size_t> (m)].value);
        const double tau = doc.macros[static_cast<std::size_t> (m)].smoothSeconds;
        ch->coef = tau <= 0.0 ? 1.0f : static_cast<float> (1.0 - std::exp (-static_cast<double> (g->maxBlock) / (tau * options.sampleRate)));
        g->channels.push_back (std::move (ch));
    }
    {
        std::vector<const GroupModel*> ordered;
        for (const auto& grp : doc.groups)
            ordered.push_back (&grp);
        auto depth = [&] (const GroupModel* grp) {
            int d = 0;
            for (const GroupModel* p = grp; p != nullptr && ! p->parent.empty() && d < 8; p = doc.findGroup (p->parent))
                ++d;
            return d;
        };
        std::stable_sort (ordered.begin(), ordered.end(), [&] (const GroupModel* a, const GroupModel* b) { return depth (a) < depth (b); });
        for (const auto* grp : ordered)
        {
            g->groupChannelBase[grp->id] = static_cast<int> (g->channels.size());
            for (const auto& c : grp->controls)
            {
                auto ch = std::make_unique<ControlChannel>();
                ch->name = c.name;
                ch->value.store (static_cast<float> (c.value), std::memory_order_relaxed);
                ch->smoothed = ch->effective = static_cast<float> (c.value);
                ch->coef = 1.0f;
                g->channels.push_back (std::move (ch));
            }
        }
        auto addTargets = [&] (ControlChannel& ch, const Macro& m) {
            for (const auto& mm : m.mappings)
            {
                MacroTarget t;
                t.min = static_cast<float> (mm.min);
                t.max = static_cast<float> (mm.max);
                t.curve = mm.curve;
                if (mm.param.rfind ("control.", 0) == 0 && doc.findGroup (mm.node) != nullptr)
                {
                    const int c = std::atoi (mm.param.c_str() + 8);
                    t.channel = g->channelIndex (mm.node, c);
                    if (t.channel < 0 || t.channel >= static_cast<int> (g->channels.size()))
                        continue;
                    g->channels[static_cast<std::size_t> (t.channel)]->hasDriver = true;
                }
                else
                {
                    const auto it = g->nodeIndexById.find (mm.node);
                    if (it == g->nodeIndexById.end() || nodeAt (it->second).desc == nullptr)
                        continue;
                    t.node = it->second;
                    t.param = nodeAt (t.node).desc->paramIndex (mm.param);
                    if (t.param < 0)
                        continue;
                    nodeAt (t.node).macroMapped[static_cast<std::size_t> (t.param)] = 1;
                }
                ch.targets.push_back (t);
            }
        };
        for (int m = 0; m < maxMacros; ++m)
            addTargets (*g->channels[static_cast<std::size_t> (m)], doc.macros[static_cast<std::size_t> (m)]);
        for (const auto* grp : ordered)
            for (std::size_t c = 0; c < grp->controls.size(); ++c)
                addTargets (*g->channels[static_cast<std::size_t> (g->channelIndex (grp->id, static_cast<int> (c)))], grp->controls[c]);
        for (int c = 0; c < static_cast<int> (g->channels.size()); ++c)
            g->channelOrder.push_back (c);
    }

    // Scenes.
    for (int s = 0; s < sceneCount; ++s)
    {
        const auto& sc = doc.scenes[static_cast<std::size_t> (s)];
        if (! sc.captured)
            continue;
        g->scenesCaptured |= static_cast<std::uint8_t> (1u << s);
        for (int m = 0; m < maxMacros; ++m)
            g->sceneMacros[static_cast<std::size_t> (s)][static_cast<std::size_t> (m)] = static_cast<float> (sc.macros[static_cast<std::size_t> (m)]);
        for (const auto& [key, value] : sc.values)
        {
            const auto slash = key.find ('/');
            if (slash == std::string::npos)
                continue;
            const auto it = g->nodeIndexById.find (key.substr (0, slash));
            if (it == g->nodeIndexById.end() || nodeAt (it->second).desc == nullptr)
                continue;
            const auto& d = *nodeAt (it->second).desc;
            const int p = d.paramIndex (key.substr (slash + 1));
            if (p < 0)
                continue;
            SceneTarget* t = nullptr;
            for (auto& existing : g->sceneTargets)
                if (existing.node == it->second && existing.param == p)
                    t = &existing;
            if (t == nullptr)
            {
                g->sceneTargets.push_back ({});
                t = &g->sceneTargets.back();
                t->node = it->second;
                t->param = p;
                const auto curve = d.params[static_cast<std::size_t> (p)].curve;
                t->stepped = curve == ParamCurve::choice || curve == ParamCurve::toggle;
            }
            t->norm[static_cast<std::size_t> (s)] = static_cast<float> (d.params[static_cast<std::size_t> (p)].normalise (value));
            t->present |= static_cast<std::uint8_t> (1u << s);
        }
    }

    // Host automation bindings.
    for (std::size_t b = 0; b < doc.hostBindings.size(); ++b)
    {
        const auto& hb = doc.hostBindings[b];
        const auto it = g->nodeIndexById.find (hb.node);
        if (hb.node.empty() || it == g->nodeIndexById.end() || nodeAt (it->second).desc == nullptr)
            continue;
        g->bindings[b].node = it->second;
        g->bindings[b].param = nodeAt (it->second).desc->paramIndex (hb.param);
        if (g->bindings[b].param < 0)
            g->bindings[b].node = -1;
    }

    g->compileMilliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
    return g;
}

} // namespace andor::modular
