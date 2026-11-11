#include "graph/Registry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace andor::modular
{

double ParamDesc::normalise (double v) const noexcept
{
    v = clampNative (v);
    if (max <= min)
        return 0.0;
    if (curve == ParamCurve::logarithmic && min > 0.0)
        return std::log (v / min) / std::log (max / min);
    return (v - min) / (max - min);
}

double ParamDesc::denormalise (double n) const noexcept
{
    n = std::clamp (n, 0.0, 1.0);
    double v;
    if (curve == ParamCurve::logarithmic && min > 0.0)
        v = min * std::pow (max / min, n);
    else
        v = min + n * (max - min);
    if (curve == ParamCurve::integer || curve == ParamCurve::choice || curve == ParamCurve::toggle)
        v = std::round (v);
    return clampNative (v);
}

double ParamDesc::clampNative (double v) const noexcept
{
    if (! std::isfinite (v))
        return def;
    return std::clamp (v, std::min (min, max), std::max (min, max));
}

std::string ParamDesc::format (double v) const
{
    char buf[64];
    if (curve == ParamCurve::choice)
    {
        const auto i = static_cast<std::size_t> (std::clamp (static_cast<int> (std::round (v)), 0, static_cast<int> (choices.size()) - 1));
        return i < choices.size() ? choices[i] : std::string ("?");
    }
    if (curve == ParamCurve::toggle)
        return v >= 0.5 ? "ON" : "OFF";
    if (curve == ParamCurve::integer)
    {
        std::snprintf (buf, sizeof buf, "%d%s%s", static_cast<int> (std::round (v)), unit.empty() ? "" : " ", unit.c_str());
        return buf;
    }
    if (unit == "%")
        std::snprintf (buf, sizeof buf, "%.0f %%", v * 100.0);
    else if (unit == "Hz" && v >= 1000.0)
        std::snprintf (buf, sizeof buf, "%.2f kHz", v / 1000.0);
    else if (unit == "s" && v < 1.0)
        std::snprintf (buf, sizeof buf, "%.0f ms", v * 1000.0);
    else if (std::abs (v) >= 100.0)
        std::snprintf (buf, sizeof buf, "%.0f %s", v, unit.c_str());
    else if (std::abs (v) >= 10.0)
        std::snprintf (buf, sizeof buf, "%.1f %s", v, unit.c_str());
    else
        std::snprintf (buf, sizeof buf, "%.2f %s", v, unit.c_str());
    std::string s (buf);
    while (! s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

int NodeDesc::inputIndex (const std::string& id) const noexcept
{
    for (std::size_t i = 0; i < inputs.size(); ++i)
        if (inputs[i].id == id)
            return static_cast<int> (i);
    return -1;
}

int NodeDesc::outputIndex (const std::string& id) const noexcept
{
    for (std::size_t i = 0; i < outputs.size(); ++i)
        if (outputs[i].id == id)
            return static_cast<int> (i);
    return -1;
}

int NodeDesc::paramIndex (const std::string& id) const noexcept
{
    for (std::size_t i = 0; i < params.size(); ++i)
        if (params[i].id == id)
            return static_cast<int> (i);
    return -1;
}

void Registry::add (NodeDesc desc)
{
    const auto key = desc.type;
    types[key] = std::move (desc);
    ordered.clear();
    for (const auto& [k, d] : types)
        ordered.push_back (&d);
    // Catalogue order: by category, then name (Quick Add browses this).
    static const char* order[] = { "Sources", "Processors", "Controllers", "Spatial", "Utilities" };
    auto rank = [] (const std::string& c) {
        for (int i = 0; i < 5; ++i)
            if (c == order[i])
                return i;
        return 5;
    };
    std::stable_sort (ordered.begin(), ordered.end(), [&] (const NodeDesc* a, const NodeDesc* b) {
        if (rank (a->category) != rank (b->category))
            return rank (a->category) < rank (b->category);
        return a->name < b->name;
    });
}

const NodeDesc* Registry::find (const std::string& type) const noexcept
{
    const auto it = types.find (type);
    return it != types.end() ? &it->second : nullptr;
}

const Registry& Registry::builtIn()
{
    static const Registry registry = [] {
        Registry r;
        registerEventNodes (r);
        registerSourceNodes (r);
        registerProcessorNodes (r);
        registerControlNodes (r);
        registerSpatialNodes (r);
        registerUtilityNodes (r);
        registerFxNodes (r);
        return r;
    }();
    return registry;
}

int fuzzyScore (const std::string& query, const std::string& candidate) noexcept
{
    if (query.empty())
        return 1;
    auto lower = [] (char c) { return static_cast<char> (std::tolower (static_cast<unsigned char> (c))); };
    // Whole-substring match scores highest, at a word start higher still.
    std::string q, c;
    for (char ch : query)
        q += lower (ch);
    for (char ch : candidate)
        c += lower (ch);
    const auto at = c.find (q);
    if (at != std::string::npos)
    {
        const bool wordStart = at == 0 || ! std::isalnum (static_cast<unsigned char> (c[at - 1]));
        return 1000 + (wordStart ? 500 : 0) - static_cast<int> (at) - static_cast<int> (c.size()) / 4;
    }
    // Subsequence.
    int score = 0, ci = 0, run = 0;
    for (const char qc : q)
    {
        bool found = false;
        while (ci < static_cast<int> (c.size()))
        {
            const bool wordStart = ci == 0 || ! std::isalnum (static_cast<unsigned char> (c[static_cast<std::size_t> (ci - 1)]));
            if (c[static_cast<std::size_t> (ci++)] == qc)
            {
                score += 10 + (wordStart ? 15 : 0) + run * 5;
                ++run;
                found = true;
                break;
            }
            run = 0;
        }
        if (! found)
            return 0;
    }
    return score;
}

std::vector<const NodeDesc*> Registry::search (const std::string& query, int maxResults) const
{
    std::vector<std::pair<int, const NodeDesc*>> scored;
    for (const auto* d : ordered)
    {
        int best = std::max ({ fuzzyScore (query, d->name) * 3, fuzzyScore (query, d->type) * 2, fuzzyScore (query, d->category) });
        for (const auto& k : d->keywords)
            best = std::max (best, fuzzyScore (query, k) * 2);
        if (query.size() >= 3)
            best = std::max (best, fuzzyScore (query, d->description) / 2);
        if (best > 0)
            scored.emplace_back (best + (d->available ? 0 : -400), d);
    }
    std::stable_sort (scored.begin(), scored.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const NodeDesc*> out;
    for (const auto& [s, d] : scored)
    {
        if (static_cast<int> (out.size()) >= maxResults)
            break;
        out.push_back (d);
    }
    return out;
}

} // namespace andor::modular
