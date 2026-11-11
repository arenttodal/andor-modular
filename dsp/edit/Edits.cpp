#include "edit/Edits.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>

namespace andor::modular::edit
{

//==============================================================================
void History::push (const Document& before, std::string label)
{
    undoStack.push_back ({ before, std::move (label) });
    if (undoStack.size() > depth)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool History::undo (Document& doc)
{
    if (undoStack.empty())
        return false;
    auto e = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back ({ doc, e.label });
    doc = std::move (e.doc);
    return true;
}

bool History::redo (Document& doc)
{
    if (redoStack.empty())
        return false;
    auto e = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back ({ doc, e.label });
    doc = std::move (e.doc);
    return true;
}

void History::clear()
{
    undoStack.clear();
    redoStack.clear();
}

//==============================================================================
namespace
{
    const NodeDesc* descOf (const Document& doc, const Registry& reg, const std::string& id)
    {
        const auto* n = doc.findNode (id);
        return n != nullptr ? reg.find (n->type) : nullptr;
    }

    std::string firstOfType (const Document& doc, const std::string& type, const std::string& group)
    {
        for (const auto& n : doc.nodes)
            if (n.type == type && n.group == group)
                return n.id;
        for (const auto& n : doc.nodes)
            if (n.type == type)
                return n.id;
        return {};
    }

    std::vector<std::string> outgoing (const Document& doc, const std::string& node, const std::string& port, bool audioOnly = true)
    {
        std::vector<std::string> r;
        for (const auto& c : doc.connections)
            if (c.from == node && (port.empty() || c.fromPort == port) && ! (audioOnly && c.toParam))
                r.push_back (c.id);
        return r;
    }

    std::string inputSource (const Document& doc, const std::string& node, const std::string& port)
    {
        for (const auto& c : doc.connections)
            if (c.to == node && c.toPort == port && ! c.toParam)
                return c.id;
        return {};
    }

    bool kindsMatch (PortKind out, PortKind in, bool acceptsControl) noexcept
    {
        if (out == PortKind::notes || in == PortKind::notes)
            return out == in;
        if (isAudio (out))
            return isAudio (in);
        return isControlLike (in) || (isAudio (in) && acceptsControl);
    }

    Result fail (std::string why)
    {
        Result r;
        r.message = std::move (why);
        return r;
    }

    std::string nameOf (const Registry& reg, const std::string& type)
    {
        const auto* d = reg.find (type);
        return d != nullptr ? d->name : type;
    }

    /** The first parameter of a node worth modulating (its Quick face's first modulatable one). */
    std::string modulationTarget (const NodeDesc& d)
    {
        for (const auto& p : d.params)
            if (p.quick && p.modulatable && p.curve != ParamCurve::choice && p.curve != ParamCurve::toggle)
                return p.id;
        for (const auto& p : d.params)
            if (p.modulatable && p.curve != ParamCurve::choice && p.curve != ParamCurve::toggle)
                return p.id;
        return {};
    }
}

/** OUTPUT gets `node`'s output: directly when free, else through a MIXER (made if needed). */
std::string routeToOutput (Document& doc, const Registry& reg, const std::string& node, const std::string& port, std::vector<std::string>& created,
                           std::string& note)
{
    auto* src = doc.findNode (node);
    std::string out = firstOfType (doc, "util.output", src != nullptr ? src->group : std::string {});
    if (out.empty())
    {
        auto* o = doc.addNode (reg, "util.output", src != nullptr ? src->x + 260.0f : 600.0f, src != nullptr ? src->y : 0.0f);
        out = o->id;
        created.push_back (out);
    }
    const auto existing = inputSource (doc, out, "in");
    if (existing.empty())
    {
        doc.connect (node, port, out, "in");
        return out;
    }
    // Occupied: a MIXER sums the old and the new.
    const auto* c = doc.findConnection (existing);
    const auto* feeder = doc.findNode (c->from);
    if (feeder != nullptr && feeder->type == "util.mixer")
    {
        for (int k = 1; k <= 4; ++k)
        {
            const auto in = "in" + std::to_string (k);
            if (inputSource (doc, feeder->id, in).empty())
            {
                doc.connect (node, port, feeder->id, in);
                note = "joined MIXER " + std::to_string (k);
                return feeder->id;
            }
        }
        note = "the MIXER is full";
        return {};
    }
    const std::string oldFrom = c->from, oldPort = c->fromPort;
    doc.disconnect (existing);
    const auto* o = doc.findNode (out);
    auto* mixer = doc.addNode (reg, "util.mixer", o->x - 30.0f, o->y + 140.0f);
    const auto mid = mixer->id;
    created.push_back (mid);
    doc.connect (oldFrom, oldPort, mid, "in1");
    doc.connect (node, port, mid, "in2");
    doc.connect (mid, "out", out, "in");
    note = "created MIXER";
    return mid;
}


//==============================================================================
Result connect (Document& doc, const Registry& reg, const std::string& from, const std::string& fromPort, const std::string& to, const std::string& toPort)
{
    Validator v (reg);
    const auto check = v.checkConnection (doc, from, fromPort, to, toPort, false);
    if (! check.ok)
    {
        auto r = fail (check.reason);
        if (! check.suggestion.empty())
            r.message += " (try " + nameOf (reg, check.suggestion) + ")";
        return r;
    }
    const auto& c = doc.connect (from, fromPort, to, toPort);
    Result r;
    r.ok = true;
    r.focus = c.id;
    r.message = check.implicitBroadcast ? "connected (every voice hears it)" : "connected";
    return r;
}

Result modulate (Document& doc, const Registry& reg, const std::string& from, const std::string& fromPort, const std::string& to, const std::string& param, double depth)
{
    Validator v (reg);
    const auto check = v.checkConnection (doc, from, fromPort, to, param, true);
    if (! check.ok)
        return fail (check.reason);
    int mods = 0;
    for (const auto& c : doc.connections)
        mods += c.toParam ? 1 : 0;
    if (mods >= v.limits().modulations)
        return fail ("the patch already has " + std::to_string (mods) + " modulation routes (the most this version runs)");
    const auto& c = doc.modulate (from, fromPort, to, param, depth);
    Result r;
    r.ok = true;
    r.focus = c.id;
    r.message = "modulating " + param;
    return r;
}

Result insertOnConnection (Document& doc, const Registry& reg, const std::string& connectionId, const std::string& type)
{
    const auto* c = doc.findConnection (connectionId);
    const auto* desc = reg.find (type);
    if (c == nullptr || desc == nullptr || c->toParam)
        return fail ("nothing to insert into");
    const Document before = doc;
    const Connection old = *c;
    const auto* a = doc.findNode (old.from);
    const auto* b = doc.findNode (old.to);
    const auto* da = reg.find (a->type);
    const auto* db = reg.find (b->type);
    const int oi = da->outputIndex (old.fromPort), ii = db->inputIndex (old.toPort);
    if (oi < 0 || ii < 0)
        return fail ("that cable's ports are unknown");
    const auto& outPort = da->outputs[static_cast<std::size_t> (oi)];
    const auto& inPort = db->inputs[static_cast<std::size_t> (ii)];
    const Domain cableDomain = Validator::portDomain (*a, *da, outPort, false);
    Domain domain = desc->defaultDomain;
    if (desc->role != NodeRole::event)
    {
        if (cableDomain == Domain::voice && desc->supports (Domain::voice))
            domain = Domain::voice;
        else if (cableDomain != Domain::voice && desc->supports (Domain::global))
            domain = Domain::global;
        else
            return fail (desc->name + " cannot sit on a " + std::string (domainName (cableDomain)) + " cable");
    }
    std::string pin, pout;
    for (const auto& p : desc->inputs)
        if (kindsMatch (outPort.kind, p.kind, p.acceptsControl))
        {
            pin = p.id;
            break;
        }
    for (const auto& p : desc->outputs)
        if (kindsMatch (p.kind, inPort.kind, inPort.acceptsControl))
        {
            pout = p.id;
            break;
        }
    if (pin.empty() || pout.empty())
        return fail (desc->name + " has no ports for this cable");
    auto* p = doc.addNode (reg, type, 0.5f * (a->x + b->x), 0.5f * (a->y + b->y) + 40.0f);
    p->domain = domain;
    const auto pid = p->id;
    doc.disconnect (connectionId);
    auto first = connect (doc, reg, old.from, old.fromPort, pid, pin);
    auto second = first.ok ? connect (doc, reg, pid, pout, old.to, old.toPort) : first;
    if (! first.ok || ! second.ok)
    {
        doc = before;
        return fail (! first.ok ? first.message : second.message);
    }
    Result r;
    r.ok = true;
    r.created = { pid };
    r.focus = pid;
    r.message = "inserted " + desc->name + " between " + doc.displayName (reg, *doc.findNode (old.from)) + " and " + doc.displayName (reg, *doc.findNode (old.to));
    return r;
}

Result addModule (Document& doc, const Registry& reg, const std::string& type, float x, float y, const std::string& selected)
{
    const auto* desc = reg.find (type);
    if (desc == nullptr)
        return fail ("unknown module " + type);
    if (! desc->available)
        return fail (desc->name + " is not available: " + desc->unavailableReason);
    Validator v (reg);
    const auto report = v.validate (doc);
    if (desc->role == NodeRole::source && report.sources >= v.limits().sources)
        return fail ("this patch already has " + std::to_string (report.sources) + " sound sources (the most this version plays)");
    if (report.nodes >= v.limits().nodes)
        return fail ("this patch already has " + std::to_string (report.nodes) + " modules (the most this version runs)");
    if (desc->isArp && report.arps >= v.limits().arps)
        return fail ("six arpeggiators already");
    if (desc->isMorph && report.morphs >= v.limits().morphs)
        return fail ("two MORPH modules already");
    if (desc->isExternal && report.externals >= v.limits().externals)
        return fail ("one external instrument per patch in this version");
    if (desc->role == NodeRole::output && ! firstOfType (doc, "util.output", doc.view.currentGroup).empty())
        return fail ("the patch already has its OUTPUT");

    Result r;
    std::vector<std::string> made;
    auto* n = doc.addNode (reg, type, x, y);
    const std::string id = n->id;
    made.push_back (id);
    r.focus = id;

    if (desc->role == NodeRole::source)
    {
        // Notes from the KEYBOARD.
        if (desc->inputIndex ("notes") >= 0)
        {
            std::string keys = firstOfType (doc, "event.keyboard", doc.view.currentGroup);
            if (keys.empty())
            {
                keys = doc.addNode (reg, "event.keyboard", x - 240.0f, y)->id;
                made.push_back (keys);
            }
            doc.connect (keys, "notes", id, "notes");
        }
        std::string note;
        if (doc.findNode (id)->domain == Domain::voice)
        {
            // source -> AMP ENVELOPE -> VOICE MIX -> OUTPUT
            auto* env = doc.addNode (reg, "proc.vca", x + 230.0f, y);
            const auto envId = env->id;
            made.push_back (envId);
            doc.connect (id, "out", envId, "in");
            std::string mix = firstOfType (doc, "util.voicemix", doc.view.currentGroup);
            if (mix.empty())
            {
                mix = doc.addNode (reg, "util.voicemix", x + 460.0f, y)->id;
                made.push_back (mix);
                doc.connect (envId, "out", mix, "in");
                routeToOutput (doc, reg, mix, "out", made, note);
            }
            else
            {
                doc.connect (envId, "out", mix, "in");
                note = "joined VOICE MIX";
            }
        }
        else
            routeToOutput (doc, reg, id, "out", made, note);
        r.message = desc->name + " plays";
        if (! note.empty())
            r.message += " (" + note + ")";
    }
    else if (desc->role == NodeRole::event && desc->type != "event.keyboard")
    {
        // An ARP / sequencer goes between the KEYBOARD and the selected source.
        const auto* sel = doc.findNode (selected);
        const auto* sd = sel != nullptr ? reg.find (sel->type) : nullptr;
        const bool takesNotes = desc->inputIndex ("notes") >= 0;
        std::string keys = firstOfType (doc, "event.keyboard", doc.view.currentGroup);
        if (sd != nullptr && sd->inputIndex ("notes") >= 0)
        {
            const auto feed = inputSource (doc, selected, "notes");
            if (! feed.empty())
                doc.disconnect (feed);
            if (takesNotes && ! keys.empty())
                doc.connect (keys, "notes", id, "notes");
            doc.connect (id, "notes", selected, "notes");
            r.message = desc->name + " now plays " + doc.displayName (reg, *sel);
        }
        else
        {
            if (takesNotes && ! keys.empty())
                doc.connect (keys, "notes", id, "notes");
            r.message = desc->name + " added: connect its NOTES output to a source";
        }
    }
    else if (desc->category == "Controllers" && ! desc->outputs.empty() && desc->outputs[0].kind != PortKind::notes)
    {
        // A modulator: onto the selected module's first musical parameter.
        const auto* sd = descOf (doc, reg, selected);
        auto* m = doc.findNode (id);
        if (sd != nullptr)
        {
            const auto* sel = doc.findNode (selected);
            if (sel->domain == Domain::voice && desc->supports (Domain::voice))
                m->domain = Domain::voice;
            const auto target = modulationTarget (*sd);
            if (! target.empty())
            {
                const auto mr = modulate (doc, reg, id, desc->outputs[0].id, selected, target, 0.3);
                r.message = mr.ok ? desc->name + " modulates " + doc.displayName (reg, *sel) + " " + target : desc->name + " added (" + mr.message + ")";
            }
        }
        else
            r.message = desc->name + " added: drag from its output onto a parameter";
    }
    else if (! selected.empty() && doc.findNode (selected) != nullptr && ! desc->inputs.empty())
    {
        // A processor after the selected module, when its main output goes one place.
        const auto* sd = descOf (doc, reg, selected);
        const auto outs = sd != nullptr && ! sd->outputs.empty() ? outgoing (doc, selected, sd->outputs[0].id) : std::vector<std::string> {};
        if (outs.size() == 1)
        {
            // Remove the placeholder made above; insert a fresh one on the cable.
            doc.removeNode (id);
            auto ins = insertOnConnection (doc, reg, outs[0], type);
            if (! ins.ok)
            {
                // fall back: keep it unconnected
                auto* again = doc.addNode (reg, type, x, y);
                ins.created = { again->id };
                ins.focus = again->id;
                ins.message = desc->name + " added (" + ins.message + ")";
                ins.ok = true;
            }
            return ins;
        }
        if (outs.empty() && sd != nullptr && ! sd->outputs.empty())
        {
            auto c = connect (doc, reg, selected, sd->outputs[0].id, id, desc->inputs[0].id);
            if (c.ok)
                doc.findNode (id)->domain = doc.findNode (selected)->domain == Domain::voice && desc->supports (Domain::voice) ? Domain::voice : desc->defaultDomain;
            r.message = c.ok ? desc->name + " follows " + doc.displayName (reg, *doc.findNode (selected)) : desc->name + " added";
        }
        else
        {
            r.message = "where should " + desc->name + " go?";
            r.choices = outs;
        }
    }
    else
        r.message = desc->name + " added";

    r.ok = true;
    r.created = made;
    return r;
}

Result addConnectedTo (Document& doc, const Registry& reg, const std::string& node, const std::string& port, bool portIsOutput, const std::string& type,
                       float x, float y)
{
    const auto* desc = reg.find (type);
    const auto* base = doc.findNode (node);
    const auto* bd = descOf (doc, reg, node);
    if (desc == nullptr || base == nullptr || bd == nullptr)
        return fail ("nothing to connect to");
    const Document before = doc;
    auto* n = doc.addNode (reg, type, x, y);
    const auto id = n->id;
    if (base->domain == Domain::voice && desc->supports (Domain::voice) && desc->role != NodeRole::event)
        n->domain = Domain::voice;
    const auto& ports = portIsOutput ? desc->inputs : desc->outputs;
    for (const auto& p : ports)
    {
        auto r = portIsOutput ? connect (doc, reg, node, port, id, p.id) : connect (doc, reg, id, p.id, node, port);
        if (r.ok)
        {
            r.created = { id };
            r.focus = id;
            r.message = desc->name + " connected";
            return r;
        }
    }
    doc = before;
    return fail (desc->name + " cannot connect there");
}

std::vector<const NodeDesc*> compatibleTypes (const Document& doc, const Registry& reg, const std::string& node, const std::string& port, bool portIsOutput)
{
    std::vector<const NodeDesc*> out;
    for (const auto* d : reg.all())
    {
        if (! d->available || d->role == NodeRole::output)
            continue;
        Document probe = doc;
        const auto r = addConnectedTo (probe, reg, node, port, portIsOutput, d->type, 0.0f, 0.0f);
        if (r.ok)
            out.push_back (d);
    }
    return out;
}

Result replaceNode (Document& doc, const Registry& reg, const std::string& node, const std::string& type)
{
    const auto* old = doc.findNode (node);
    const auto* desc = reg.find (type);
    if (old == nullptr || desc == nullptr)
        return fail ("nothing to replace");
    const NodeModel was = *old;
    auto* n = doc.addNode (reg, type, was.x, was.y);
    const auto id = n->id;
    n->group = was.group;
    n->label = was.label;
    if (desc->supports (was.domain))
        n->domain = was.domain;
    for (const auto& [k, v] : was.params)
        if (desc->paramIndex (k) >= 0)
            n->params[k] = desc->params[static_cast<std::size_t> (desc->paramIndex (k))].clampNative (v);
    for (const auto& [k, v] : was.strings)
        if (n->strings.count (k) > 0)
            n->strings[k] = v;
    std::vector<Connection> touching;
    for (const auto& c : doc.connections)
        if (c.from == node || c.to == node)
            touching.push_back (c);
    for (auto& m : doc.macros)
        for (auto& mm : m.mappings)
            if (mm.node == node && desc->paramIndex (mm.param) >= 0)
                mm.node = id;
    for (auto& b : doc.hostBindings)
        if (b.node == node && desc->paramIndex (b.param) >= 0)
            b.node = id;
    doc.removeNode (node);
    int kept = 0, dropped = 0;
    for (auto c : touching)
    {
        if (c.from == node)
            c.from = id;
        if (c.to == node)
            c.to = id;
        Validator v (reg);
        if (v.checkConnection (doc, c.from, c.fromPort, c.to, c.toPort, c.toParam).ok)
        {
            auto& made = c.toParam ? doc.modulate (c.from, c.fromPort, c.to, c.toPort, c.depth) : doc.connect (c.from, c.fromPort, c.to, c.toPort);
            made.transpose = c.transpose;
            ++kept;
        }
        else
            ++dropped;
    }
    Result r;
    r.ok = true;
    r.created = { id };
    r.focus = id;
    r.message = "replaced by " + desc->name + " (" + std::to_string (kept) + " cables kept" + (dropped > 0 ? ", " + std::to_string (dropped) + " had no place" : std::string {}) + ")";
    return r;
}

Result dropOnSource (Document& doc, const Registry& reg, const std::string& dragged, const std::string& target, DropAction action)
{
    auto* d = doc.findNode (dragged);
    auto* t = doc.findNode (target);
    const auto* dd = descOf (doc, reg, dragged);
    const auto* td = descOf (doc, reg, target);
    if (d == nullptr || t == nullptr || dd == nullptr || td == nullptr || dragged == target)
        return fail ("drop a source onto another source");
    const Document before = doc;

    auto audioOut = [] (const NodeDesc& desc) -> std::string {
        for (const auto& p : desc.outputs)
            if (isAudio (p.kind))
                return p.id;
        return {};
    };
    const auto dOut = audioOut (*dd), tOut = audioOut (*td);
    if (dOut.empty() || tOut.empty())
        return fail ("both need an audio output");

    if (action == DropAction::layer)
    {
        if (! outgoing (doc, dragged, dOut).empty())
        {
            Result r;
            r.ok = true;
            r.message = "both already sound together";
            return r;
        }
        std::vector<std::string> made;
        std::string note;
        if (d->domain == Domain::voice)
        {
            auto* env = doc.addNode (reg, "proc.vca", d->x + 220.0f, d->y);
            const auto envId = env->id;
            made.push_back (envId);
            doc.connect (dragged, dOut, envId, "in");
            auto mix = firstOfType (doc, "util.voicemix", t->group);
            if (mix.empty())
                return fail ("no VOICE MIX to layer into");
            doc.connect (envId, "out", mix, "in");
        }
        else
            routeToOutput (doc, reg, dragged, dOut, made, note);
        Result r;
        r.ok = true;
        r.created = made;
        r.message = "layered";
        return r;
    }

    // MORPH, MODULATE and REPLACE free the dragged source's outputs first; an AMP ENVELOPE left
    // with nothing to play is removed with them.
    auto freeOutputs = [&] (const std::string& node, const std::string& port) {
        std::vector<std::string> orphans;
        for (const auto& cid : outgoing (doc, node, port))
        {
            const auto* c = doc.findConnection (cid);
            const auto to = c->to;
            doc.disconnect (cid);
            const auto* n = doc.findNode (to);
            if (n != nullptr && n->type == "proc.vca" && inputSource (doc, to, "in").empty())
                orphans.push_back (to);
        }
        for (const auto& o : orphans)
            doc.removeNode (o);
    };

    if (action == DropAction::morph)
    {
        const bool voice = d->domain == Domain::voice && t->domain == Domain::voice;
        const bool global = d->domain == Domain::global && t->domain == Domain::global;
        if (! voice && ! global)
            return fail ("a per-voice source and a global one cannot morph note by note: put GLOBAL MORPH after VOICE MIX");
        const std::string type = voice ? "proc.morph" : "proc.morph.global";
        Validator v (reg);
        if (v.validate (doc).morphs >= v.limits().morphs)
            return fail ("two MORPH modules already");
        const auto targetOuts = outgoing (doc, target, tOut);
        std::vector<Connection> destinations;
        for (const auto& cid : targetOuts)
            destinations.push_back (*doc.findConnection (cid));
        for (const auto& cid : targetOuts)
            doc.disconnect (cid);
        freeOutputs (dragged, dOut);
        auto* m = doc.addNode (reg, type, std::max (d->x, t->x) + 200.0f, 0.5f * (d->y + t->y));
        const auto mid = m->id;
        bool ok = connect (doc, reg, target, tOut, mid, "a").ok && connect (doc, reg, dragged, dOut, mid, "b").ok;
        for (const auto& dest : destinations)
            ok = ok && connect (doc, reg, mid, "out", dest.to, dest.toPort).ok;
        if (! ok)
        {
            doc = before;
            return fail ("MORPH could not be wired here");
        }
        Result r;
        r.ok = true;
        r.created = { mid };
        r.focus = mid;
        r.message = std::string (voice ? "MORPH" : "GLOBAL MORPH") + ": A " + doc.displayName (reg, *doc.findNode (target)) + ", B "
                    + doc.displayName (reg, *doc.findNode (dragged));
        return r;
    }

    if (action == DropAction::modulate)
    {
        if (td->inputIndex ("fm") < 0)
            return fail (td->name + " has no FM input: drop onto an oscillator");
        if (d->domain != t->domain)
            return fail ("the modulator must run where the oscillator runs (both per voice)");
        freeOutputs (dragged, dOut);
        auto c = connect (doc, reg, dragged, dOut, target, "fm");
        if (! c.ok)
        {
            doc = before;
            return c;
        }
        auto* tn = doc.findNode (target);
        if (tn->param ("fm", 0.0) <= 0.0)
            tn->params["fm"] = 0.3;
        Result r;
        r.ok = true;
        r.message = doc.displayName (reg, *doc.findNode (dragged)) + " frequency-modulates " + doc.displayName (reg, *tn);
        return r;
    }

    // REPLACE: the dragged source takes the target's place in its chain.
    freeOutputs (dragged, dOut);
    std::vector<Connection> touching;
    for (const auto& c : doc.connections)
        if (c.from == target || c.to == target)
            touching.push_back (c);
    const float tx = t->x, ty = t->y;
    doc.removeNode (target);
    auto* dn = doc.findNode (dragged);
    dn->x = tx;
    dn->y = ty;
    int kept = 0;
    for (auto c : touching)
    {
        if (c.from == target)
            c.from = dragged;
        if (c.to == target)
            c.to = dragged;
        if (c.from == dragged && c.fromPort == tOut)
            c.fromPort = dOut;
        Validator v (reg);
        if (v.checkConnection (doc, c.from, c.fromPort, c.to, c.toPort, c.toParam).ok)
        {
            c.toParam ? doc.modulate (c.from, c.fromPort, c.to, c.toPort, c.depth) : doc.connect (c.from, c.fromPort, c.to, c.toPort);
            ++kept;
        }
    }
    Result r;
    r.ok = true;
    r.focus = dragged;
    r.message = "replaced (" + std::to_string (kept) + " cables kept)";
    return r;
}

Result duplicate (Document& doc, const Registry& reg, const std::vector<std::string>& nodes, bool withLinks, float dx, float dy)
{
    std::map<std::string, std::string> map;
    Result r;
    Validator v (reg);
    const auto report = v.validate (doc);
    int sources = report.sources;
    for (const auto& id : nodes)
    {
        const auto* n = doc.findNode (id);
        if (n == nullptr)
            continue;
        const auto* d = reg.find (n->type);
        if (d != nullptr && d->role == NodeRole::source && ++sources > v.limits().sources)
            return fail ("duplicating would exceed six sound sources");
        if (d != nullptr && d->role == NodeRole::output)
            continue;
        NodeModel copy = *n;
        copy.id = doc.makeId ("n");
        copy.x += dx;
        copy.y += dy;
        map[id] = copy.id;
        doc.nodes.push_back (copy);
        r.created.push_back (copy.id);
    }
    const auto conns = doc.connections;
    for (const auto& c : conns)
    {
        const bool fromIn = map.count (c.from) > 0, toIn = map.count (c.to) > 0;
        if (! toIn || (! fromIn && ! withLinks))
            continue;
        auto& made = c.toParam ? doc.modulate (fromIn ? map[c.from] : c.from, c.fromPort, map[c.to], c.toPort, c.depth)
                               : doc.connect (fromIn ? map[c.from] : c.from, c.fromPort, map[c.to], c.toPort);
        made.transpose = c.transpose;
    }
    // Outputs of the copies go where the originals' went when those inputs take several cables.
    if (withLinks)
        for (const auto& c : conns)
            if (map.count (c.from) > 0 && map.count (c.to) == 0 && ! c.toParam)
            {
                Validator vv (reg);
                if (vv.checkConnection (doc, map[c.from], c.fromPort, c.to, c.toPort, false).ok)
                    doc.connect (map[c.from], c.fromPort, c.to, c.toPort);
            }
    r.ok = ! r.created.empty();
    r.focus = r.created.empty() ? std::string {} : r.created.front();
    r.message = r.ok ? "duplicated " + std::to_string (r.created.size()) + (withLinks ? " with links" : "") : "nothing to duplicate";
    return r;
}

void removeNodes (Document& doc, const std::vector<std::string>& nodes)
{
    for (const auto& id : nodes)
        doc.removeNode (id);
}

namespace
{
    /** A card's height on the canvas (the editor's NodeView layout: header, port rows, one mode
        row, one knob row, the live strip), so arranged cards never overlap. */
    float cardHeight (const Document& doc, const Registry& reg, const std::string& id)
    {
        int rows = 0, knobs = 0, choices = 0;
        if (const auto* g = doc.findGroup (id))
        {
            int in = 0, out = 0;
            for (const auto& p : g->ports)
                (p.input ? in : out)++;
            rows = std::max (in, out);
            knobs = std::min<int> (4, static_cast<int> (g->controls.size()));
        }
        else if (const auto* n = doc.findNode (id))
        {
            const auto* d = reg.find (n->type);
            if (d != nullptr)
            {
                rows = std::max<int> (static_cast<int> (d->inputs.size()), d->role == NodeRole::output ? 0 : static_cast<int> (d->outputs.size()));
                for (const auto& p : d->params)
                    if (p.quick && d->paramShown (p, n->params))
                        (p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle ? choices : knobs)++;
                choices = std::min (choices, 1);
                knobs = std::min (knobs, 4);
            }
        }
        return 28.0f + 6.0f + 20.0f * static_cast<float> (rows) + (choices > 0 ? 28.0f : 0.0f) + (knobs > 0 ? 54.0f : 0.0f) + 30.0f;
    }
}

void autoArrange (Document& doc, const Registry& reg, const std::vector<std::string>& keep)
{
    const std::string group = doc.view.currentGroup;
    // Units: the modules of this view and its collapsed child groups.
    auto owner = [&] (const std::string& node) -> std::string {
        const auto* n = doc.findNode (node);
        if (n == nullptr)
            return {};
        if (n->group == group)
            return node;
        std::string g = n->group;
        for (int guard = 0; guard < 8 && ! g.empty(); ++guard)
        {
            const auto* gm = doc.findGroup (g);
            if (gm == nullptr)
                return {};
            if (gm->parent == group)
                return g;
            g = gm->parent;
        }
        return {};
    };
    std::map<std::string, int> depth;
    std::vector<std::string> ids;
    for (const auto& g : doc.groups)
        if (g.parent == group)
        {
            ids.push_back (g.id);
            depth[g.id] = 1;
        }
    for (const auto& n : doc.nodes)
        if (n.group == group)
        {
            ids.push_back (n.id);
            const auto* d = reg.find (n.type);
            depth[n.id] = d != nullptr && d->role == NodeRole::event ? 0 : 1;
        }
    // Longest path (bounded: cycles through FEEDBACK DELAY settle).
    for (int pass = 0; pass < 32; ++pass)
    {
        bool changed = false;
        for (const auto& c : doc.connections)
        {
            const auto a = owner (c.from), b = owner (c.to);
            if (a.empty() || b.empty() || a == b)
                continue;
            const auto* dt = descOf (doc, reg, c.to);
            if (dt != nullptr && dt->role == NodeRole::feedback)
                continue;
            const int want = depth[a] + (c.toParam ? 0 : 1);
            if (want > depth[b] && want < 40)
            {
                depth[b] = want;
                changed = true;
            }
        }
        if (! changed)
            break;
    }
    int last = 0;
    for (const auto& [id, d] : depth)
        if (const auto* desc = descOf (doc, reg, id); desc == nullptr || desc->role != NodeRole::output)
            last = std::max (last, d);
    for (const auto& id : ids)
        if (const auto* d = descOf (doc, reg, id); d != nullptr && d->role == NodeRole::output)
            depth[id] = last + 1;
    auto positionOf = [&] (const std::string& id) -> std::pair<float*, float*> {
        if (auto* n = doc.findNode (id))
            return { &n->x, &n->y };
        auto* g = doc.findGroup (id);
        return { &g->x, &g->y };
    };
    std::map<int, std::vector<std::string>> columns;
    for (const auto& id : ids)
        columns[depth[id]].push_back (id);
    for (auto& [col, members] : columns)
    {
        std::stable_sort (members.begin(), members.end(), [&] (const auto& a, const auto& b) { return *positionOf (a).second < *positionOf (b).second; });
        float total = 0.0f;
        for (const auto& id : members)
            total += cardHeight (doc, reg, id) + 32.0f;
        float y = -0.5f * total;
        for (const auto& id : members)
        {
            if (std::find (keep.begin(), keep.end(), id) == keep.end())
            {
                auto [x, yy] = positionOf (id);
                *x = static_cast<float> (col) * 256.0f;
                *yy = std::round (y / 8.0f) * 8.0f;
            }
            y += cardHeight (doc, reg, id) + 32.0f;
        }
    }
}

//==============================================================================
bool setParam (Document& doc, const Registry& reg, const std::string& node, const std::string& param, double native)
{
    auto* n = doc.findNode (node);
    const auto* d = n != nullptr ? reg.find (n->type) : nullptr;
    const int p = d != nullptr ? d->paramIndex (param) : -1;
    if (p < 0)
        return false;
    n->params[param] = d->params[static_cast<std::size_t> (p)].clampNative (native);
    return true;
}

bool mapMacro (Document& doc, const Registry& reg, int index, const std::string& node, const std::string& param, double min, double max, MacroCurve curve)
{
    if (index < 0 || index >= maxMacros)
        return false;
    const bool groupControl = doc.findGroup (node) != nullptr && param.rfind ("control.", 0) == 0;
    const auto* d = descOf (doc, reg, node);
    if (! groupControl && (d == nullptr || d->paramIndex (param) < 0))
        return false;
    auto& m = doc.macros[static_cast<std::size_t> (index)];
    for (const auto& mm : m.mappings)
        if (mm.node == node && mm.param == param)
            return false;
    if (m.mappings.size() >= 16)
        return false;
    m.mappings.push_back ({ node, param, std::clamp (min, 0.0, 1.0), std::clamp (max, 0.0, 1.0), curve });
    if (m.name.empty())
        m.name = groupControl ? doc.findGroup (node)->name : d->params[static_cast<std::size_t> (d->paramIndex (param))].name;
    if (m.mappings.size() == 1 && groupControl)
    {
        // Start the macro where the group control is now.
        const auto* g = doc.findGroup (node);
        const auto c = static_cast<std::size_t> (std::atoi (param.c_str() + 8));
        if (c < g->controls.size())
        {
            const double span = max - min;
            m.value = m.defaultValue = std::abs (span) > 1.0e-9 ? std::clamp ((g->controls[c].value - min) / span, 0.0, 1.0) : 0.0;
        }
    }
    if (m.mappings.size() == 1 && ! groupControl)
    {
        // Start the macro where the parameter is now (nothing jumps when it is assigned).
        const auto& pd = d->params[static_cast<std::size_t> (d->paramIndex (param))];
        const double norm = pd.normalise (doc.paramValue (reg, node, param));
        const double span = max - min;
        m.value = m.defaultValue = std::abs (span) > 1.0e-9 ? std::clamp ((norm - min) / span, 0.0, 1.0) : 0.0;
    }
    return true;
}

void captureScene (Document& doc, const Registry& reg, int index, const std::string& name)
{
    if (index < 0 || index >= sceneCount)
        return;
    auto& s = doc.scenes[static_cast<std::size_t> (index)];
    s.values.clear();
    for (const auto& n : doc.nodes)
        if (const auto* d = reg.find (n.type))
            for (const auto& p : d->params)
                if (! p.structural)
                    s.values[n.id + "/" + p.id] = n.param (p.id, p.def);
    for (int m = 0; m < maxMacros; ++m)
        s.macros[static_cast<std::size_t> (m)] = doc.macros[static_cast<std::size_t> (m)].value;
    s.captured = true;
    if (! name.empty())
        s.name = name;
    else if (s.name.empty())
    {
        static const char* names[sceneCount] = { "I", "II", "III", "IV" };
        s.name = names[index];
    }
}

bool recallScene (Document& doc, const Registry& reg, int index)
{
    if (index < 0 || index >= sceneCount || ! doc.scenes[static_cast<std::size_t> (index)].captured)
        return false;
    applyScenePosition (doc, reg, static_cast<double> (index));
    doc.currentScene = index;
    return true;
}

void applyScenePosition (Document& doc, const Registry& reg, double position)
{
    position = std::clamp (position, 0.0, static_cast<double> (sceneCount - 1));
    int a = -1, b = -1;
    for (int s = 0; s < sceneCount; ++s)
        if (doc.scenes[static_cast<std::size_t> (s)].captured)
        {
            if (s <= position)
                a = s;
            if (s >= position && b < 0)
                b = s;
        }
    if (a < 0)
        a = b;
    if (b < 0)
        b = a;
    if (a < 0)
        return;
    const double t = a == b ? 0.0 : (position - a) / static_cast<double> (b - a);
    const auto& sa = doc.scenes[static_cast<std::size_t> (a)];
    const auto& sb = doc.scenes[static_cast<std::size_t> (b)];
    for (auto& n : doc.nodes)
    {
        const auto* d = reg.find (n.type);
        if (d == nullptr)
            continue;
        for (const auto& p : d->params)
        {
            const auto key = n.id + "/" + p.id;
            const auto ia = sa.values.find (key), ib = sb.values.find (key);
            if (ia == sa.values.end() && ib == sb.values.end())
                continue;
            const double va = ia != sa.values.end() ? ia->second : ib->second;
            const double vb = ib != sb.values.end() ? ib->second : va;
            const bool stepped = p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle || p.curve == ParamCurve::integer;
            n.params[p.id] = stepped ? (t < 0.5 ? va : vb) : p.denormalise (p.normalise (va) + (p.normalise (vb) - p.normalise (va)) * t);
        }
    }
    for (int m = 0; m < maxMacros; ++m)
        doc.macros[static_cast<std::size_t> (m)].value = sa.macros[static_cast<std::size_t> (m)] + (sb.macros[static_cast<std::size_t> (m)] - sa.macros[static_cast<std::size_t> (m)]) * t;
    doc.scenePosition = position;
}

} // namespace andor::modular::edit
