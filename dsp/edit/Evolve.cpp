#include "edit/Edits.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace andor::modular::edit
{

namespace
{
    bool contains (const std::string& s, const char* part) { return s.find (part) != std::string::npos; }

    bool isLevel (const std::string& id)
    {
        return id == "level" || contains (id, "level") || id == "gain" || id == "master" || id == "output" || id == "makeup" || id == "mute"
               || id == "velocity";
    }
    bool isScale (const std::string& id)
    {
        return id == "scale" || id == "root" || id == "quantise" || id == "semitones" || id == "octave" || id == "low" || id == "range"
               || id == "key" || id == "coarse";
    }
    bool isRhythm (const NodeDesc& d, const std::string& id)
    {
        if (d.role == NodeRole::event)
            return id == "rate" || id == "pattern" || id == "swing" || id == "length" || id == "gate" || id == "division" || id == "density"
                   || id == "bars" || id == "probability" || id[0] == 'g' || id == "variation";
        return id == "division" || id == "sync";
    }
    /** Parameters EVOLVE leaves alone at every intensity: structure and plumbing. */
    bool isPlumbing (const std::string& id)
    {
        return id == "keytrack" || id == "phaseReset" || id == "levelMatch" || id == "channel" || id == "high" || id == "follow" || id == "hard"
               || id == "clamp" || id == "unipolar" || id == "retrigger" || id == "output" || id == "comp" || id == "invert" || id == "ms";
    }
}

EvolveResult evolve (const Document& original, const Registry& reg, const EvolveOptions& options)
{
    EvolveResult result;
    using I = EvolveOptions::Intensity;
    const int level = options.intensity == I::subtle ? 0 : (options.intensity == I::moderate ? 1 : 2);
    const double chance[3] { 0.25, 0.45, 0.65 };
    const double reach[3] { 0.06, 0.18, 0.4 };

    std::set<std::string> protect (options.protect.begin(), options.protect.end());
    for (const auto& id : options.protect)
        if (original.findGroup (id) != nullptr)
            for (const auto& member : groupMembers (original, id))
                protect.insert (member);
    std::vector<std::string> scope = options.scope;
    if (scope.empty())
        for (const auto& n : original.nodes)
            scope.push_back (n.id);
    else
    {
        // A group in the scope means its members.
        std::vector<std::string> expanded;
        for (const auto& id : scope)
        {
            if (original.findGroup (id) != nullptr)
                for (const auto& m : groupMembers (original, id))
                    expanded.push_back (m);
            else
                expanded.push_back (id);
        }
        scope = expanded;
    }

    Validator validator (reg);
    const auto before = validator.validate (original);
    for (int attempt = 0; attempt < std::max (1, options.maxAttempts); ++attempt)
    {
        result.attempts = attempt + 1;
        osp::Prng r (osp::Prng::deriveSeed (options.seed, static_cast<std::uint64_t> (attempt), static_cast<std::uint64_t> (level)));
        Document d = original;
        std::vector<std::string> changes;

        // 1. Parameters.
        for (const auto& id : scope)
        {
            if (protect.count (id) > 0)
                continue;
            auto* n = d.findNode (id);
            const auto* desc = n != nullptr ? reg.find (n->type) : nullptr;
            if (desc == nullptr)
                continue;
            for (const auto& p : desc->params)
            {
                if (p.structural || isPlumbing (p.id) || (options.preserveLevel && isLevel (p.id)) || (options.lockScale && isScale (p.id))
                    || (options.lockRhythm && isRhythm (*desc, p.id)))
                    continue;
                const bool discrete = p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle;
                if (discrete && level == 0)
                    continue;   // SUBTLE never switches a mode
                double weight = p.quick ? 1.0 : 0.5;
                if (r.nextDouble() > chance[level] * weight)
                    continue;
                const double was = n->param (p.id, p.def);
                double now = was;
                if (discrete)
                {
                    const int count = static_cast<int> (std::round (p.max - p.min)) + 1;
                    if (count < 2)
                        continue;
                    if (level == 1)
                        now = std::clamp (was + (r.nextDouble() < 0.5 ? -1.0 : 1.0), p.min, p.max);   // a neighbour
                    else
                        now = p.min + static_cast<double> (r.nextBelow (static_cast<std::uint64_t> (count)));
                }
                else
                {
                    const double norm = p.normalise (was) + r.bipolar() * reach[level];
                    now = json::roundSignificant (p.denormalise (std::clamp (norm, 0.0, 1.0)), 12, std::max (std::abs (p.min), std::abs (p.max)));
                }
                if (std::abs (now - was) < 1.0e-9)
                    continue;
                n->params[p.id] = now;
                changes.push_back (d.displayName (reg, *n) + " " + p.name + ": " + p.format (was) + " > " + p.format (now));
            }
        }

        // 2. Modulation depths (MODERATE and WILD).
        if (level >= 1)
            for (auto& c : d.connections)
                if (c.toParam && protect.count (c.to) == 0 && std::find (scope.begin(), scope.end(), c.to) != scope.end() && r.nextDouble() < 0.5)
                {
                    const double was = c.depth;
                    c.depth = json::roundSignificant (std::clamp (c.depth + r.bipolar() * reach[level], -1.0, 1.0), 12, 1.0);
                    changes.push_back ("modulation depth " + std::to_string (static_cast<int> (std::round (was * 100))) + "% > "
                                       + std::to_string (static_cast<int> (std::round (c.depth * 100))) + "%");
                }

        // 3. Structure (WILD, routing not locked): a processor on a cable, a new modulation.
        if (level == 2 && ! options.lockRouting)
        {
            std::vector<std::string> cables;
            for (const auto& c : d.connections)
                if (! c.toParam && protect.count (c.from) == 0 && protect.count (c.to) == 0)
                {
                    const auto* from = d.findNode (c.from);
                    const auto* fd = from != nullptr ? reg.find (from->type) : nullptr;
                    if (fd != nullptr && fd->outputIndex (c.fromPort) >= 0 && isAudio (fd->outputs[static_cast<std::size_t> (fd->outputIndex (c.fromPort))].kind))
                        cables.push_back (c.id);
                }
            static const char* pool[] = { "proc.filter", "proc.drive", "proc.tone", "fx.phaser", "fx.flanger", "fx.chorus", "fx.movement" };
            if (! cables.empty() && r.nextDouble() < 0.7)
            {
                const auto cable = cables[static_cast<std::size_t> (r.nextBelow (cables.size()))];
                const auto type = pool[r.nextBelow (sizeof pool / sizeof pool[0])];
                const auto ins = insertOnConnection (d, reg, cable, type);
                if (ins.ok)
                    changes.push_back (ins.message);
            }
            // A modulator (an existing LFO / RANDOM) onto a new parameter.
            std::vector<std::string> modulators;
            for (const auto& n : d.nodes)
                if ((n.type == "ctl.lfo" || n.type == "ctl.random") && protect.count (n.id) == 0)
                    modulators.push_back (n.id);
            if (! modulators.empty() && r.nextDouble() < 0.6)
            {
                const auto from = modulators[static_cast<std::size_t> (r.nextBelow (modulators.size()))];
                std::vector<std::pair<std::string, std::string>> targets;
                for (const auto& id : scope)
                {
                    const auto* n = d.findNode (id);
                    const auto* desc = n != nullptr ? reg.find (n->type) : nullptr;
                    if (desc == nullptr || protect.count (id) > 0 || id == from)
                        continue;
                    for (const auto& p : desc->params)
                        if (p.modulatable && p.quick && p.curve != ParamCurve::choice && p.curve != ParamCurve::toggle && ! (options.preserveLevel && isLevel (p.id))
                            && ! (options.lockScale && isScale (p.id)))
                            targets.emplace_back (id, p.id);
                }
                if (! targets.empty())
                {
                    const auto& [to, param] = targets[static_cast<std::size_t> (r.nextBelow (targets.size()))];
                    const auto m = modulate (d, reg, from, "out", to, param, json::roundSignificant (0.15 + 0.3 * r.nextDouble()));
                    if (m.ok)
                        changes.push_back (d.displayName (reg, *d.findNode (from)) + " now modulates " + d.displayName (reg, *d.findNode (to)) + " " + param);
                }
            }
        }

        // Accept only a patch that is as valid as the original, within budgets, without a new
        // loop and with something changed.
        const auto after = validator.validate (d);
        int feedbackBefore = 0, feedbackAfter = 0;
        for (const auto& n : original.nodes)
            feedbackBefore += n.type == "util.feedback" ? 1 : 0;
        for (const auto& n : d.nodes)
            feedbackAfter += n.type == "util.feedback" ? 1 : 0;
        const bool valid = ! after.hasErrors() || before.hasErrors();
        if (! valid || feedbackAfter > feedbackBefore || changes.empty() || after.cost > validator.limits().costUnits * 1.25f)
            continue;
        d.evolve.seed = options.seed;
        d.evolve.generation = original.evolve.generation + 1;
        result.ok = true;
        result.doc = std::move (d);
        result.changes = std::move (changes);
        return result;
    }
    result.doc = original;
    return result;
}

} // namespace andor::modular::edit
