#pragma once

#include "graph/Node.h"
#include "graph/Registry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

/** Helpers that keep node descriptors short and uniform (the dsp/nodes files). */
namespace andor::modular::kit
{

inline PortDesc in (std::string id, std::string name, PortKind kind, bool multi = false)
{
    PortDesc p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.kind = kind;
    p.multi = multi;
    p.polarity = kind == PortKind::gate || kind == PortKind::trigger || kind == PortKind::clock ? Polarity::unipolar : Polarity::bipolar;
    return p;
}

inline PortDesc out (std::string id, std::string name, PortKind kind, Polarity polarity = Polarity::bipolar)
{
    PortDesc p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.kind = kind;
    p.polarity = polarity;
    return p;
}

inline PortDesc modIn (std::string id, std::string name)
{
    auto p = in (std::move (id), std::move (name), PortKind::audioMono);
    p.acceptsControl = true;
    return p;
}

inline PortDesc notesIn() { return in ("notes", "NOTES", PortKind::notes, true); }
inline PortDesc notesOut() { return out ("notes", "NOTES", PortKind::notes); }

inline ParamDesc num (std::string id, std::string name, double min, double max, double def, std::string unit = {}, bool quick = false,
                      ParamCurve curve = ParamCurve::linear)
{
    ParamDesc p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.min = min;
    p.max = max;
    p.def = def;
    p.unit = std::move (unit);
    p.quick = quick;
    p.curve = curve;
    return p;
}

inline ParamDesc logNum (std::string id, std::string name, double min, double max, double def, std::string unit = {}, bool quick = false)
{
    return num (std::move (id), std::move (name), min, max, def, std::move (unit), quick, ParamCurve::logarithmic);
}

inline ParamDesc integer (std::string id, std::string name, int min, int max, int def, std::string unit = {}, bool quick = false)
{
    auto p = num (std::move (id), std::move (name), min, max, def, std::move (unit), quick, ParamCurve::integer);
    return p;
}

inline ParamDesc choice (std::string id, std::string name, std::vector<std::string> choices, int def = 0, bool quick = false)
{
    ParamDesc p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.min = 0.0;
    p.max = static_cast<double> (std::max<std::size_t> (1, choices.size()) - 1);
    p.def = def;
    p.curve = ParamCurve::choice;
    p.choices = std::move (choices);
    p.quick = quick;
    p.modulatable = false;
    return p;
}

inline ParamDesc toggle (std::string id, std::string name, bool def = false, bool quick = false)
{
    ParamDesc p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.min = 0.0;
    p.max = 1.0;
    p.def = def ? 1.0 : 0.0;
    p.curve = ParamCurve::toggle;
    p.quick = quick;
    p.modulatable = false;
    return p;
}

template <typename P>
std::function<std::unique_ptr<Processor>()> make()
{
    return [] { return std::make_unique<P>(); };
}

template <typename P>
std::function<std::unique_ptr<EventProcessor>()> makeEvent()
{
    return [] { return std::make_unique<P>(); };
}

/** One-pole smoothing applied once per chunk (a parameter glides over ~`seconds`). */
struct Smoothed
{
    float value = 0.0f;
    bool primed = false;
    float next (float target, float seconds, const ChunkContext& ctx) noexcept
    {
        if (! primed || seconds <= 0.0f)
        {
            value = target;
            primed = true;
            return value;
        }
        const float a = 1.0f - std::exp (-static_cast<float> (ctx.numSamples) / (seconds * static_cast<float> (ctx.sampleRate)));
        value += a * (target - value);
        return value;
    }
    void reset() noexcept { primed = false; }
};

/** Linear ramp across a chunk from `from` to `to` (gain changes without zipper noise). */
inline float rampAt (float from, float to, int i, int n) noexcept
{
    return from + (to - from) * (static_cast<float> (i + 1) / static_cast<float> (n));
}

inline double noteToHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }
inline float dbToGain (double db) noexcept { return db <= -96.0 ? 0.0f : static_cast<float> (std::pow (10.0, db / 20.0)); }

inline void clear (ProcessIO& io, int port, int n, int channels = 2) noexcept
{
    for (int c = 0; c < channels; ++c)
        std::fill (io.out[static_cast<std::size_t> (port)][static_cast<std::size_t> (c)], io.out[static_cast<std::size_t> (port)][static_cast<std::size_t> (c)] + n, 0.0f);
}

/** Equal-power pan gains for p in -1..1. */
inline void panGains (float p, float& l, float& r) noexcept
{
    const float a = (std::clamp (p, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    l = std::cos (a);
    r = std::sin (a);
}

} // namespace andor::modular::kit
