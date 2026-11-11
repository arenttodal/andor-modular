#include "edit/Edits.h"

#include <algorithm>
#include <map>
#include <set>

namespace andor::modular::edit
{

std::vector<std::string> groupMembers (const Document& doc, const std::string& groupId)
{
    std::set<std::string> groups { groupId };
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const auto& g : doc.groups)
            if (groups.count (g.parent) > 0 && groups.insert (g.id).second)
                grew = true;
    }
    std::vector<std::string> out;
    for (const auto& n : doc.nodes)
        if (groups.count (n.group) > 0)
            out.push_back (n.id);
    return out;
}

Result group (Document& doc, const Registry& reg, const std::vector<std::string>& nodes, const std::string& name)
{
    Result r;
    std::vector<std::string> members;
    for (const auto& id : nodes)
        if (const auto* n = doc.findNode (id); n != nullptr && n->group == doc.view.currentGroup)
            members.push_back (id);
    if (members.empty())
    {
        r.message = "select modules to group";
        return r;
    }
    // One nested level shown in this version: a group inside a group inside a group is refused.
    int depth = 0;
    for (const auto* g = doc.findGroup (doc.view.currentGroup); g != nullptr && depth < 8; g = doc.findGroup (g->parent))
        ++depth;
    if (depth >= 2)
    {
        r.message = "groups nest one level deep in this version";
        return r;
    }
    GroupModel g;
    g.id = doc.makeId ("g");
    g.name = name.empty() ? "GROUP" : name;
    g.parent = doc.view.currentGroup;
    // The card takes the place of its first (top-left) member.
    float mx = 1.0e9f, my = 1.0e9f;
    for (const auto& id : members)
    {
        auto* n = doc.findNode (id);
        n->group = g.id;
        mx = std::min (mx, n->x);
        my = std::min (my, n->y);
    }
    g.x = mx;
    g.y = my;
    const std::set<std::string> inside (members.begin(), members.end());
    std::set<std::string> seen;
    for (const auto& c : doc.connections)
    {
        const bool from = inside.count (c.from) > 0, to = inside.count (c.to) > 0;
        if (from == to)
            continue;
        ExposedPort p;
        p.input = to;
        p.node = to ? c.to : c.from;
        p.port = to ? c.toPort : c.fromPort;
        const auto key = p.node + "/" + p.port + (p.input ? "<" : ">") + (c.toParam ? "p" : "");
        if (! seen.insert (key).second)
            continue;
        const auto* d = reg.find (doc.findNode (p.node)->type);
        std::string portName = p.port;
        if (d != nullptr)
        {
            if (c.toParam && to)
            {
                const int pi = d->paramIndex (p.port);
                portName = pi >= 0 ? d->params[static_cast<std::size_t> (pi)].name : p.port;
            }
            else
            {
                const int pi = to ? d->inputIndex (p.port) : d->outputIndex (p.port);
                if (pi >= 0)
                    portName = (to ? d->inputs : d->outputs)[static_cast<std::size_t> (pi)].name;
            }
        }
        p.name = portName;
        g.ports.push_back (p);
    }
    doc.groups.push_back (g);
    r.ok = true;
    r.focus = g.id;
    r.message = "grouped " + std::to_string (members.size()) + " modules as " + g.name + " (" + std::to_string (g.ports.size()) + " ports)";
    return r;
}

Result ungroup (Document& doc, const std::string& groupId)
{
    Result r;
    const auto* g = doc.findGroup (groupId);
    if (g == nullptr)
    {
        r.message = "no such group";
        return r;
    }
    const auto parent = g->parent;
    for (auto& n : doc.nodes)
        if (n.group == groupId)
            n.group = parent;
    for (auto& other : doc.groups)
        if (other.parent == groupId)
            other.parent = parent;
    for (auto& m : doc.macros)
        m.mappings.erase (std::remove_if (m.mappings.begin(), m.mappings.end(), [&] (const MacroMapping& mm) { return mm.node == groupId; }), m.mappings.end());
    doc.groups.erase (std::remove_if (doc.groups.begin(), doc.groups.end(), [&] (const GroupModel& x) { return x.id == groupId; }), doc.groups.end());
    if (doc.view.currentGroup == groupId)
        doc.view.currentGroup = parent;
    r.ok = true;
    r.message = "ungrouped";
    return r;
}

bool exposeControl (Document& doc, const Registry& reg, const std::string& groupId, const std::string& node, const std::string& param,
                    const std::string& name, double min, double max)
{
    auto* g = doc.findGroup (groupId);
    if (g == nullptr || g->controls.size() >= maxMacros)
        return false;
    const auto members = groupMembers (doc, groupId);
    const bool nestedControl = doc.findGroup (node) != nullptr && param.rfind ("control.", 0) == 0;
    if (! nestedControl && std::find (members.begin(), members.end(), node) == members.end())
        return false;
    Macro m;
    if (! nestedControl)
    {
        const auto* n = doc.findNode (node);
        const auto* d = reg.find (n->type);
        const int pi = d != nullptr ? d->paramIndex (param) : -1;
        if (pi < 0)
            return false;
        const auto& pd = d->params[static_cast<std::size_t> (pi)];
        m.name = name.empty() ? pd.name : name;
        const double span = max - min;
        m.value = m.defaultValue = std::abs (span) > 1.0e-9 ? std::clamp ((pd.normalise (n->param (param, pd.def)) - min) / span, 0.0, 1.0) : 0.0;
    }
    else
        m.name = name.empty() ? "CONTROL" : name;
    m.mappings.push_back ({ node, param, std::clamp (min, 0.0, 1.0), std::clamp (max, 0.0, 1.0), MacroCurve::linear });
    g->controls.push_back (m);
    return true;
}

std::string exportModule (const Document& doc, const std::string& groupId)
{
    const auto* g = doc.findGroup (groupId);
    if (g == nullptr)
        return {};
    const auto members = groupMembers (doc, groupId);
    const std::set<std::string> inside (members.begin(), members.end());
    // A document holding just the group: its nodes, nested groups and inner cables.
    Document part;
    part.name = g->name;
    part.settings = doc.settings;
    for (const auto& n : doc.nodes)
        if (inside.count (n.id) > 0)
            part.nodes.push_back (n);
    std::set<std::string> groups { groupId };
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const auto& other : doc.groups)
            if (groups.count (other.parent) > 0 && groups.insert (other.id).second)
                grew = true;
    }
    for (const auto& other : doc.groups)
        if (groups.count (other.id) > 0)
        {
            auto copy = other;
            if (copy.id == groupId)
                copy.parent.clear();
            part.groups.push_back (copy);
        }
    for (const auto& c : doc.connections)
        if (inside.count (c.from) > 0 && inside.count (c.to) > 0)
            part.connections.push_back (c);
    auto v = toJson (part);
    v.set ("format", "andormodule");
    v.set ("module", g->name);
    v.set ("root", groupId);
    // The content hash covers what the module plays (nodes and cables), not where it sits.
    auto body = json::Value::object();
    body.set ("nodes", v["nodes"]);
    body.set ("connections", v["connections"]);
    v.set ("hash", contentHash (json::write (body, -1)));
    return json::write (v, 2);
}

Result importModule (Document& doc, const Registry& reg, const std::string& moduleText, float x, float y)
{
    Result r;
    const auto parsed = json::parse (moduleText);
    if (! parsed.ok)
    {
        r.message = "unreadable module: " + parsed.error;
        return r;
    }
    if (parsed.value.getString ("format") != "andormodule")
    {
        r.message = "not an ANDOR Modular module";
        return r;
    }
    Document part;
    const auto loaded = fromJson (parsed.value, part);
    if (! loaded.ok)
    {
        r.message = loaded.error;
        return r;
    }
    const auto rootOld = parsed.value.getString ("root");
    const Document before = doc;
    std::map<std::string, std::string> ids;
    for (const auto& g : part.groups)
        ids[g.id] = doc.makeId ("g");
    for (const auto& n : part.nodes)
        ids[n.id] = doc.makeId ("n");
    auto mapped = [&] (const std::string& id) { return ids.count (id) > 0 ? ids[id] : id; };
    // Positions: the module's centre lands at (x, y).
    float cx = 0.0f, cy = 0.0f;
    for (const auto& n : part.nodes)
    {
        cx += n.x;
        cy += n.y;
    }
    if (! part.nodes.empty())
    {
        cx /= static_cast<float> (part.nodes.size());
        cy /= static_cast<float> (part.nodes.size());
    }
    for (auto n : part.nodes)
    {
        n.id = mapped (n.id);
        n.group = mapped (n.group);
        n.x += x - cx;
        n.y += y - cy;
        doc.nodes.push_back (n);
    }
    for (auto g : part.groups)
    {
        const bool root = g.id == rootOld;
        g.id = mapped (g.id);
        g.parent = root ? doc.view.currentGroup : mapped (g.parent);
        g.x = x;
        g.y = y;
        g.module = parsed.value.getString ("module");
        for (auto& p : g.ports)
            p.node = mapped (p.node);
        for (auto& c : g.controls)
            for (auto& mm : c.mappings)
                mm.node = mapped (mm.node);
        doc.groups.push_back (g);
    }
    for (auto c : part.connections)
    {
        c.id = doc.makeId ("c");
        c.from = mapped (c.from);
        c.to = mapped (c.to);
        doc.connections.push_back (c);
    }
    Validator v (reg);
    const auto report = v.validate (doc);
    if (report.hasErrors())
    {
        std::string why;
        for (const auto& i : report.issues)
            if (i.severity == Issue::Severity::error)
            {
                why = i.message;
                break;
            }
        doc = before;
        r.message = "the module does not fit: " + why;
        return r;
    }
    r.ok = true;
    r.focus = mapped (rootOld);
    for (const auto& n : part.nodes)
        r.created.push_back (mapped (n.id));
    r.message = "added " + parsed.value.getString ("module") + " (" + std::to_string (part.nodes.size()) + " modules)";
    return r;
}

} // namespace andor::modular::edit
