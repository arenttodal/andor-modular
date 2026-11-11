#include "graph/Document.h"

#include "graph/Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace andor::modular
{

//==============================================================================
NodeModel* Document::findNode (const std::string& id) noexcept
{
    for (auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

const NodeModel* Document::findNode (const std::string& id) const noexcept
{
    for (const auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

Connection* Document::findConnection (const std::string& id) noexcept
{
    for (auto& c : connections)
        if (c.id == id)
            return &c;
    return nullptr;
}

GroupModel* Document::findGroup (const std::string& id) noexcept
{
    for (auto& g : groups)
        if (g.id == id)
            return &g;
    return nullptr;
}

const GroupModel* Document::findGroup (const std::string& id) const noexcept
{
    for (const auto& g : groups)
        if (g.id == id)
            return &g;
    return nullptr;
}

int Document::nodeIndex (const std::string& id) const noexcept
{
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id)
            return static_cast<int> (i);
    return -1;
}

std::string Document::makeId (const char* prefix)
{
    while (true)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "%s-%04llx", prefix, static_cast<unsigned long long> (idCounter++));
        const std::string id (buf);
        bool used = findNode (id) != nullptr || findGroup (id) != nullptr;
        for (const auto& c : connections)
            used = used || c.id == id;
        if (! used)
            return id;
    }
}

NodeModel* Document::addNode (const Registry& registry, const std::string& type, float x, float y)
{
    const auto* desc = registry.find (type);
    if (desc == nullptr)
        return nullptr;
    NodeModel n;
    n.id = makeId ("n");
    n.type = type;
    n.typeVersion = desc->version;
    n.domain = desc->defaultDomain;
    n.x = x;
    n.y = y;
    for (const auto& p : desc->params)
        n.params[p.id] = p.def;
    for (const auto& s : desc->strings)
        n.strings[s] = {};
    n.group = view.currentGroup;
    nodes.push_back (std::move (n));
    return &nodes.back();
}

void Document::removeNode (const std::string& id)
{
    connections.erase (std::remove_if (connections.begin(), connections.end(), [&] (const Connection& c) { return c.from == id || c.to == id; }),
                       connections.end());
    auto stripMappings = [&] (Macro& m) {
        m.mappings.erase (std::remove_if (m.mappings.begin(), m.mappings.end(), [&] (const MacroMapping& mm) { return mm.node == id; }),
                          m.mappings.end());
    };
    for (auto& m : macros)
        stripMappings (m);
    for (auto& g : groups)
    {
        for (auto& m : g.controls)
            stripMappings (m);
        g.ports.erase (std::remove_if (g.ports.begin(), g.ports.end(), [&] (const ExposedPort& p) { return p.node == id; }), g.ports.end());
    }
    for (auto& b : hostBindings)
        if (b.node == id)
            b = {};
    const std::string prefix = id + "/";
    for (auto& s : scenes)
        for (auto it = s.values.begin(); it != s.values.end();)
            it = it->first.rfind (prefix, 0) == 0 ? s.values.erase (it) : std::next (it);
    nodes.erase (std::remove_if (nodes.begin(), nodes.end(), [&] (const NodeModel& n) { return n.id == id; }), nodes.end());
}

Connection& Document::connect (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& toPort)
{
    Connection c;
    c.id = makeId ("c");
    c.from = from;
    c.fromPort = fromPort;
    c.to = to;
    c.toPort = toPort;
    connections.push_back (std::move (c));
    return connections.back();
}

Connection& Document::modulate (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& param, double depth)
{
    auto& c = connect (from, fromPort, to, param);
    c.toParam = true;
    c.depth = std::clamp (depth, -1.0, 1.0);
    return c;
}

void Document::disconnect (const std::string& connectionId)
{
    connections.erase (std::remove_if (connections.begin(), connections.end(), [&] (const Connection& c) { return c.id == connectionId; }),
                       connections.end());
}

double Document::paramValue (const Registry& registry, const std::string& node, const std::string& param) const
{
    const auto* n = findNode (node);
    if (n == nullptr)
        return 0.0;
    const auto it = n->params.find (param);
    if (it != n->params.end())
        return it->second;
    if (const auto* d = registry.find (n->type))
        if (const int i = d->paramIndex (param); i >= 0)
            return d->params[static_cast<std::size_t> (i)].def;
    return 0.0;
}

std::vector<std::string> Document::assetKeys() const
{
    std::vector<std::string> keys;
    for (const auto& n : nodes)
        for (const auto& [k, v] : n.strings)
            if ((k == "sample" || k == "plugin") && ! v.empty() && std::find (keys.begin(), keys.end(), v) == keys.end())
                keys.push_back (v);
    return keys;
}

std::string Document::displayName (const Registry& registry, const NodeModel& node) const
{
    if (! node.label.empty())
        return node.label;
    if (const auto* d = registry.find (node.type))
        return d->name;
    return "MISSING " + node.type;
}

//==============================================================================
const char* macroCurveName (MacroCurve c) noexcept
{
    switch (c)
    {
        case MacroCurve::linear: return "linear";
        case MacroCurve::inverted: return "inverted";
        case MacroCurve::logarithmic: return "log";
        case MacroCurve::exponential: return "exp";
        case MacroCurve::bipolar: return "bipolar";
    }
    return "linear";
}

static MacroCurve macroCurveFrom (const std::string& s) noexcept
{
    if (s == "inverted") return MacroCurve::inverted;
    if (s == "log") return MacroCurve::logarithmic;
    if (s == "exp") return MacroCurve::exponential;
    if (s == "bipolar") return MacroCurve::bipolar;
    return MacroCurve::linear;
}

double applyMacroCurve (MacroCurve c, double x) noexcept
{
    x = std::clamp (x, 0.0, 1.0);
    switch (c)
    {
        case MacroCurve::linear: return x;
        case MacroCurve::inverted: return 1.0 - x;
        case MacroCurve::logarithmic: return std::log1p (9.0 * x) / std::log (10.0);   // fast start
        case MacroCurve::exponential: return x * x;                                     // slow start
        case MacroCurve::bipolar: return 0.5 + 0.5 * std::tanh (3.0 * (x - 0.5)) / std::tanh (1.5);   // S around the centre
    }
    return x;
}

//==============================================================================
std::string contentHash (const void* data, std::size_t size)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto* p = static_cast<const unsigned char*> (data);
    for (std::size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    char buf[20];
    std::snprintf (buf, sizeof buf, "%016llx", static_cast<unsigned long long> (h));
    return buf;
}

std::string contentHash (const std::string& text) { return contentHash (text.data(), text.size()); }

//==============================================================================
namespace
{
    json::Value macroToJson (const Macro& m)
    {
        auto v = json::Value::object();
        v.set ("name", m.name);
        v.set ("value", m.value);
        v.set ("default", m.defaultValue);
        v.set ("color", m.color);
        v.set ("smooth", m.smoothSeconds);
        auto maps = json::Value::array();
        for (const auto& mm : m.mappings)
        {
            auto e = json::Value::object();
            e.set ("node", mm.node);
            e.set ("param", mm.param);
            e.set ("min", mm.min);
            e.set ("max", mm.max);
            e.set ("curve", macroCurveName (mm.curve));
            maps.push (std::move (e));
        }
        v.set ("mappings", std::move (maps));
        return v;
    }

    Macro macroFromJson (const json::Value& v)
    {
        Macro m;
        m.name = v.getString ("name");
        m.value = std::clamp (v.get ("value", 0.0), 0.0, 1.0);
        m.defaultValue = std::clamp (v.get ("default", m.value), 0.0, 1.0);
        m.color = std::clamp (v.getInt ("color", 0), 0, 15);
        m.smoothSeconds = std::clamp (v.get ("smooth", 0.03), 0.0, 2.0);
        for (const auto& e : v["mappings"].elements())
        {
            MacroMapping mm;
            mm.node = e.getString ("node");
            mm.param = e.getString ("param");
            mm.min = std::clamp (e.get ("min", 0.0), 0.0, 1.0);
            mm.max = std::clamp (e.get ("max", 1.0), 0.0, 1.0);
            mm.curve = macroCurveFrom (e.getString ("curve", "linear"));
            if (! mm.node.empty() && ! mm.param.empty() && m.mappings.size() < 32)
                m.mappings.push_back (mm);
        }
        return m;
    }

    float clampCoord (double v) noexcept { return static_cast<float> (std::clamp (std::isfinite (v) ? v : 0.0, -100000.0, 100000.0)); }

    std::string seedToString (std::uint64_t s)
    {
        char buf[24];
        std::snprintf (buf, sizeof buf, "%llu", static_cast<unsigned long long> (s));
        return buf;
    }

    std::uint64_t seedFrom (const json::Value& v, std::uint64_t fallback)
    {
        if (v.isString())
        {
            std::uint64_t r = 0;
            for (const char c : v.asString())
            {
                if (c < '0' || c > '9')
                    return fallback;
                r = r * 10 + static_cast<std::uint64_t> (c - '0');
            }
            return r;
        }
        if (v.isNumber() && v.asNumber() >= 0.0 && v.asNumber() < 9.0e15)
            return static_cast<std::uint64_t> (v.asNumber());
        return fallback;
    }

    /** Schema 0 (the kernel's pre-release form, tests/fixtures/patch-v0.json): "edges" with
        "src"/"dst" as "node:port", node positions as "pos": [x, y]. Moved to version 1. */
    json::Value migrateV0 (const json::Value& v0)
    {
        json::Value v1 = v0;
        auto nodes = json::Value::array();
        for (const auto& n : v0["nodes"].elements())
        {
            auto copy = n;
            if (n["pos"].isArray())
            {
                copy.set ("x", n["pos"][0].asNumber());
                copy.set ("y", n["pos"][1].asNumber());
            }
            nodes.push (std::move (copy));
        }
        v1.set ("nodes", std::move (nodes));
        auto conns = json::Value::array();
        for (const auto& e : v0["edges"].elements())
        {
            auto split = [] (const std::string& s, std::string& node, std::string& port) {
                const auto colon = s.find (':');
                node = s.substr (0, colon);
                port = colon == std::string::npos ? std::string {} : s.substr (colon + 1);
            };
            std::string fn, fp, tn, tp;
            split (e.getString ("src"), fn, fp);
            split (e.getString ("dst"), tn, tp);
            auto c = json::Value::object();
            c.set ("id", e.getString ("id"));
            c.set ("from", fn);
            c.set ("fromPort", fp);
            c.set ("to", tn);
            c.set ("toPort", tp);
            conns.push (std::move (c));
        }
        v1.set ("connections", std::move (conns));
        v1.set ("schemaVersion", 1);
        return v1;
    }
}

json::Value toJson (const Document& doc)
{
    auto root = json::Value::object();
    root.set ("format", Document::format);
    root.set ("schemaVersion", Document::schemaVersion);
    root.set ("name", doc.name);
    root.set ("author", doc.author);
    root.set ("description", doc.description);

    auto s = json::Value::object();
    s.set ("polyphony", doc.settings.polyphony);
    s.set ("voiceMode", doc.settings.voiceMode);
    s.set ("glide", doc.settings.glideSeconds);
    s.set ("seed", seedToString (doc.settings.seed));
    s.set ("masterDb", doc.settings.masterDb);
    s.set ("bendRange", doc.settings.bendRange);
    s.set ("limiter", doc.settings.limiter);
    root.set ("settings", std::move (s));

    auto nodes = json::Value::array();
    for (const auto& n : doc.nodes)
    {
        auto v = json::Value::object();
        v.set ("id", n.id);
        v.set ("type", n.type);
        v.set ("version", n.typeVersion);
        v.set ("domain", domainName (n.domain));
        v.set ("x", static_cast<double> (n.x));
        v.set ("y", static_cast<double> (n.y));
        v.set ("size", n.size);
        v.set ("enabled", n.enabled);
        if (! n.label.empty())
            v.set ("label", n.label);
        if (! n.group.empty())
            v.set ("group", n.group);
        auto params = json::Value::object();
        for (const auto& [k, value] : n.params)   // std::map: sorted, deterministic
            params.set (k, value);
        v.set ("params", std::move (params));
        bool anyString = false;
        auto strings = json::Value::object();
        for (const auto& [k, value] : n.strings)
            if (! value.empty())
            {
                strings.set (k, value);
                anyString = true;
            }
        if (anyString)
            v.set ("strings", std::move (strings));
        nodes.push (std::move (v));
    }
    root.set ("nodes", std::move (nodes));

    auto conns = json::Value::array();
    for (const auto& c : doc.connections)
    {
        auto v = json::Value::object();
        v.set ("id", c.id);
        v.set ("from", c.from);
        v.set ("fromPort", c.fromPort);
        v.set ("to", c.to);
        v.set ("toPort", c.toPort);
        if (c.toParam)
        {
            v.set ("toParam", true);
            v.set ("depth", c.depth);
        }
        if (c.transpose != 0)
            v.set ("transpose", c.transpose);
        conns.push (std::move (v));
    }
    root.set ("connections", std::move (conns));

    auto groups = json::Value::array();
    for (const auto& g : doc.groups)
    {
        auto v = json::Value::object();
        v.set ("id", g.id);
        v.set ("name", g.name);
        if (! g.parent.empty())
            v.set ("parent", g.parent);
        v.set ("x", static_cast<double> (g.x));
        v.set ("y", static_cast<double> (g.y));
        v.set ("collapsed", g.collapsed);
        if (! g.module.empty())
            v.set ("module", g.module);
        auto ports = json::Value::array();
        for (const auto& p : g.ports)
        {
            auto e = json::Value::object();
            e.set ("name", p.name);
            e.set ("node", p.node);
            e.set ("port", p.port);
            e.set ("input", p.input);
            ports.push (std::move (e));
        }
        v.set ("ports", std::move (ports));
        auto controls = json::Value::array();
        for (const auto& m : g.controls)
            controls.push (macroToJson (m));
        v.set ("controls", std::move (controls));
        groups.push (std::move (v));
    }
    root.set ("groups", std::move (groups));

    auto macros = json::Value::array();
    for (const auto& m : doc.macros)
        macros.push (macroToJson (m));
    root.set ("macros", std::move (macros));

    auto scenes = json::Value::array();
    for (const auto& sc : doc.scenes)
    {
        auto v = json::Value::object();
        v.set ("name", sc.name);
        v.set ("captured", sc.captured);
        auto values = json::Value::object();
        for (const auto& [k, value] : sc.values)
            values.set (k, value);
        v.set ("values", std::move (values));
        auto mv = json::Value::array();
        for (const double m : sc.macros)
            mv.push (m);
        v.set ("macros", std::move (mv));
        scenes.push (std::move (v));
    }
    root.set ("scenes", std::move (scenes));
    root.set ("scenePosition", doc.scenePosition);
    root.set ("currentScene", doc.currentScene);

    auto bindings = json::Value::array();
    for (std::size_t i = 0; i < doc.hostBindings.size(); ++i)
        if (! doc.hostBindings[i].node.empty())
        {
            auto e = json::Value::object();
            e.set ("slot", static_cast<int> (i));
            e.set ("node", doc.hostBindings[i].node);
            e.set ("param", doc.hostBindings[i].param);
            bindings.push (std::move (e));
        }
    root.set ("hostBindings", std::move (bindings));

    auto view = json::Value::object();
    view.set ("zoom", doc.view.zoom);
    view.set ("panX", doc.view.panX);
    view.set ("panY", doc.view.panY);
    view.set ("group", doc.view.currentGroup);
    view.set ("play", doc.view.playView);
    view.set ("panelLayout", doc.view.panelLayout);
    view.set ("visualisation", doc.view.visualisation);
    root.set ("view", std::move (view));

    auto evolve = json::Value::object();
    evolve.set ("seed", seedToString (doc.evolve.seed));
    evolve.set ("generation", doc.evolve.generation);
    root.set ("evolve", std::move (evolve));
    root.set ("idCounter", seedToString (doc.idCounter));

    auto deps = json::Value::array();
    for (const auto& k : doc.assetKeys())
        deps.push (k);
    root.set ("dependencies", std::move (deps));
    return root;
}

LoadResult fromJson (const json::Value& input, Document& out)
{
    LoadResult result;
    if (! input.isObject())
    {
        result.error = "not a patch (expected an object)";
        return result;
    }
    const auto fmt = input.getString ("format", Document::format);
    if (fmt != Document::format && fmt != "andormodule")
    {
        result.error = "not an ANDOR Modular patch (format '" + fmt + "')";
        return result;
    }
    json::Value v = input;
    const int version = input.getInt ("schemaVersion", 0);
    if (version > Document::schemaVersion)
    {
        result.error = "made by a newer ANDOR Modular (schema " + std::to_string (version) + ")";
        return result;
    }
    if (version == 0)
    {
        v = migrateV0 (input);
        result.migratedFrom = 0;
        result.warnings.push_back ("migrated from schema 0");
    }

    Document doc;
    doc.name = v.getString ("name", "Untitled");
    doc.author = v.getString ("author");
    doc.description = v.getString ("description");
    const auto& s = v["settings"];
    doc.settings.polyphony = std::clamp (s.getInt ("polyphony", 8), 1, maxVoices);
    doc.settings.voiceMode = std::clamp (s.getInt ("voiceMode", 0), 0, 2);
    doc.settings.glideSeconds = std::clamp (s.get ("glide", 0.0), 0.0, 5.0);
    doc.settings.seed = seedFrom (s["seed"], 1);
    doc.settings.masterDb = std::clamp (s.get ("masterDb", 0.0), -60.0, 12.0);
    doc.settings.bendRange = std::clamp (s.get ("bendRange", 2.0), 0.0, 48.0);
    doc.settings.limiter = s.getBool ("limiter", true);

    const auto& registry = Registry::builtIn();
    for (const auto& n : v["nodes"].elements())
    {
        if (doc.nodes.size() >= 512)
        {
            result.warnings.push_back ("more than 512 nodes: the rest were not loaded");
            break;
        }
        NodeModel m;
        m.id = n.getString ("id");
        m.type = n.getString ("type");
        if (m.id.empty() || m.type.empty() || doc.findNode (m.id) != nullptr)
        {
            result.warnings.push_back ("a node without a unique id or type was skipped");
            continue;
        }
        m.typeVersion = n.getInt ("version", 1);
        Domain d = Domain::global;
        const auto* desc = registry.find (m.type);
        m.domain = domainFromName (n.getString ("domain"), d) ? d : (desc != nullptr ? desc->defaultDomain : Domain::global);
        if (desc != nullptr && ! desc->supports (m.domain))
            m.domain = desc->defaultDomain;
        if (desc == nullptr)
            result.warnings.push_back ("unknown module '" + m.type + "' kept as a missing module (" + m.id + ")");
        m.x = clampCoord (n.get ("x", 0.0));
        m.y = clampCoord (n.get ("y", 0.0));
        m.size = std::clamp (n.getInt ("size", 1), 0, 2);
        m.enabled = n.getBool ("enabled", true);
        m.label = n.getString ("label");
        m.group = n.getString ("group");
        if (desc != nullptr)
            for (const auto& p : desc->params)
                m.params[p.id] = p.def;
        for (const auto& [k, value] : n["params"].entries())
        {
            if (! value.isNumber())
                continue;
            double x = value.asNumber();
            if (desc != nullptr)
            {
                const int pi = desc->paramIndex (k);
                if (pi >= 0)
                    x = desc->params[static_cast<std::size_t> (pi)].clampNative (x);
            }
            m.params[k] = x;   // unknown params are kept (a newer node version's state survives)
        }
        if (desc != nullptr)
            for (const auto& key : desc->strings)
                m.strings[key] = {};
        for (const auto& [k, value] : n["strings"].entries())
            if (value.isString())
                m.strings[k] = value.asString();
        doc.nodes.push_back (std::move (m));
    }

    for (const auto& c : v["connections"].elements())
    {
        if (doc.connections.size() >= 1024)
            break;
        Connection e;
        e.id = c.getString ("id");
        e.from = c.getString ("from");
        e.fromPort = c.getString ("fromPort");
        e.to = c.getString ("to");
        e.toPort = c.getString ("toPort");
        e.toParam = c.getBool ("toParam", false);
        e.depth = std::clamp (c.get ("depth", 1.0), -1.0, 1.0);
        e.transpose = std::clamp (c.getInt ("transpose", 0), -48, 48);
        if (e.id.empty())
            e.id = "c-load-" + std::to_string (doc.connections.size());
        if (doc.findNode (e.from) == nullptr || doc.findNode (e.to) == nullptr)
        {
            result.warnings.push_back ("a connection to a node that is not in the patch was dropped (" + e.id + ")");
            continue;
        }
        doc.connections.push_back (std::move (e));
    }

    for (const auto& g : v["groups"].elements())
    {
        GroupModel m;
        m.id = g.getString ("id");
        if (m.id.empty() || doc.findGroup (m.id) != nullptr)
            continue;
        m.name = g.getString ("name", "GROUP");
        m.parent = g.getString ("parent");
        m.x = clampCoord (g.get ("x", 0.0));
        m.y = clampCoord (g.get ("y", 0.0));
        m.collapsed = g.getBool ("collapsed", true);
        m.module = g.getString ("module");
        for (const auto& p : g["ports"].elements())
            m.ports.push_back ({ p.getString ("name"), p.getString ("node"), p.getString ("port"), p.getBool ("input", true) });
        for (const auto& c : g["controls"].elements())
            if (m.controls.size() < maxMacros)
                m.controls.push_back (macroFromJson (c));
        doc.groups.push_back (std::move (m));
    }
    // Groups pointing at parents that are not there are lifted to the top level.
    for (auto& g : doc.groups)
        if (! g.parent.empty() && (doc.findGroup (g.parent) == nullptr || g.parent == g.id))
            g.parent.clear();
    for (auto& n : doc.nodes)
        if (! n.group.empty() && doc.findGroup (n.group) == nullptr)
            n.group.clear();

    for (std::size_t i = 0; i < maxMacros && i < v["macros"].size(); ++i)
        doc.macros[i] = macroFromJson (v["macros"][i]);
    for (std::size_t i = 0; i < sceneCount && i < v["scenes"].size(); ++i)
    {
        const auto& sc = v["scenes"][i];
        auto& d = doc.scenes[i];
        d.name = sc.getString ("name");
        d.captured = sc.getBool ("captured", false);
        for (const auto& [k, value] : sc["values"].entries())
            if (value.isNumber())
                d.values[k] = value.asNumber();
        for (std::size_t m = 0; m < maxMacros; ++m)
            d.macros[m] = std::clamp (sc["macros"][m].asNumber (0.0), 0.0, 1.0);
    }
    doc.scenePosition = std::clamp (v.get ("scenePosition", 0.0), 0.0, static_cast<double> (sceneCount - 1));
    doc.currentScene = std::clamp (v.getInt ("currentScene", 0), 0, sceneCount - 1);

    for (const auto& b : v["hostBindings"].elements())
    {
        const int slot = b.getInt ("slot", -1);
        if (slot >= 0 && slot < hostBankSize)
            doc.hostBindings[static_cast<std::size_t> (slot)] = { b.getString ("node"), b.getString ("param") };
    }

    const auto& view = v["view"];
    doc.view.zoom = std::clamp (view.get ("zoom", 1.0), 0.2, 3.0);
    doc.view.panX = std::clamp (view.get ("panX", 0.0), -100000.0, 100000.0);
    doc.view.panY = std::clamp (view.get ("panY", 0.0), -100000.0, 100000.0);
    doc.view.currentGroup = view.getString ("group");
    if (doc.findGroup (doc.view.currentGroup) == nullptr)
        doc.view.currentGroup.clear();
    doc.view.playView = view.getBool ("play", false);
    doc.view.panelLayout = std::clamp (view.getInt ("panelLayout", 0), 0, 2);
    doc.view.visualisation = view.getString ("visualisation", "scope");

    doc.evolve.seed = seedFrom (v["evolve"]["seed"], 1);
    doc.evolve.generation = std::max (0, v["evolve"].getInt ("generation", 0));
    doc.idCounter = std::max<std::uint64_t> (seedFrom (v["idCounter"], 1), 1);   // (makeId skips ids in use)

    out = std::move (doc);
    result.ok = true;
    return result;
}

std::string saveToString (const Document& doc) { return json::write (toJson (doc), 2); }

LoadResult loadFromString (const std::string& text, Document& doc)
{
    const auto parsed = json::parse (text);
    if (! parsed.ok)
    {
        LoadResult r;
        r.error = "unreadable patch: " + parsed.error + " at byte " + std::to_string (parsed.errorOffset);
        return r;
    }
    return fromJson (parsed.value, doc);
}

bool saveToFile (const Document& doc, const std::string& path, std::string& error)
{
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (! f)
        {
            error = "cannot write " + tmp;
            return false;
        }
        f << saveToString (doc);
        if (! f.good())
        {
            error = "write failed: " + tmp;
            return false;
        }
    }
    if (std::rename (tmp.c_str(), path.c_str()) != 0)
    {
        std::remove (path.c_str());   // Windows: rename does not replace
        if (std::rename (tmp.c_str(), path.c_str()) != 0)
        {
            error = "cannot replace " + path;
            return false;
        }
    }
    return true;
}

LoadResult loadFromFile (const std::string& path, Document& doc)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        LoadResult r;
        r.error = "cannot open " + path;
        return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromString (ss.str(), doc);
}

} // namespace andor::modular
