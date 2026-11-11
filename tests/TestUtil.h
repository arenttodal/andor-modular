#pragma once

#include "edit/Edits.h"
#include "graph/Compiler.h"
#include "graph/Engine.h"
#include "graph/Registry.h"
#include "io/AssetLoader.h"
#include "io/OfflineRender.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace test
{
using namespace andor::modular;

inline const Registry& reg() { return Registry::builtIn(); }

inline std::string presetPath (const std::string& relative) { return std::string (MODULAR_SOURCE_DIR) + "/presets/" + relative; }

/** Keyboard -> one ANALOG OSC -> AMP ENV -> VOICE MIX -> OUTPUT, made by Quick Add. */
inline Document basicPatch (std::string* osc = nullptr)
{
    Document d;
    const auto r = edit::addModule (d, reg(), "osc.analog", 0, 0);
    REQUIRE (r.ok);
    if (osc != nullptr)
        *osc = r.focus;
    return d;
}

inline std::string nodeOfType (const Document& d, const std::string& type, int nth = 0)
{
    for (const auto& n : d.nodes)
        if (n.type == type && nth-- == 0)
            return n.id;
    return {};
}

inline RenderResult render (OfflineRenderer& r, const Document& d, std::vector<ScoreNote> notes, double seconds = 1.0, int block = 256,
                            double rate = 48000.0, AssetResolver assets = {})
{
    RenderSettings s;
    s.sampleRate = rate;
    s.blockSize = block;
    s.seconds = seconds;
    s.notes = std::move (notes);
    s.assets = std::move (assets);
    return r.render (d, s);
}

inline double rms (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size())
        to = x.size();
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return to > from ? std::sqrt (s / static_cast<double> (to - from)) : 0.0;
}

inline bool finite (const std::vector<float>& x)
{
    for (const float v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}

/** Fundamental by autocorrelation (Hz) over x[from, from + length). */
inline double fundamental (const std::vector<float>& x, double rate, std::size_t from, std::size_t length, double lo = 40.0, double hi = 2000.0)
{
    const auto minLag = static_cast<std::size_t> (rate / hi), maxLag = static_cast<std::size_t> (rate / lo);
    double best = -1.0;
    std::size_t bestLag = minLag;
    std::vector<double> r (maxLag + 2, 0.0);
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0, e1 = 0.0, e2 = 0.0;
        for (std::size_t i = from; i < from + length; ++i)
        {
            s += static_cast<double> (x[i]) * x[i + lag];
            e1 += static_cast<double> (x[i]) * x[i];
            e2 += static_cast<double> (x[i + lag]) * x[i + lag];
        }
        r[lag] = s / std::sqrt (e1 * e2 + 1.0e-20);
    }
    // the first lag whose correlation is within 5 % of the best (avoids octave errors)
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
        best = std::max (best, r[lag]);
    for (std::size_t lag = minLag + 1; lag < maxLag; ++lag)
        if (r[lag] >= 0.95 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])
        {
            bestLag = lag;
            break;
        }
    // parabolic refinement
    const double a = r[bestLag - 1], b = r[bestLag], c = r[bestLag + 1];
    const double shift = (a - 2.0 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
    return rate / (static_cast<double> (bestLag) + shift);
}

/** Energy (sum of squares) of x in a band (Goertzel, at one frequency). */
inline double toneLevel (const std::vector<float>& x, double rate, double hz, std::size_t from, std::size_t length)
{
    const double w = 2.0 * 3.141592653589793 * hz / rate, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < from + length; ++i)
    {
        const double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / static_cast<double> (length);
}

} // namespace test
