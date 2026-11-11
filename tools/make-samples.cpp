// make-samples: ANDOR Modular's own factory recordings, synthesised deterministically (seeded,
// no third-party audio): a bowed C3, a ticking clock loop at 120 BPM and a rain texture.
//   make-samples OUTDIR

#include "io/Wav.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace andor::modular;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double rate = 48000.0;

    /** A two-pole resonator (band-pass) for bodies and droplets. */
    struct Resonator
    {
        double a1 = 0, a2 = 0, b0 = 0, y1 = 0, y2 = 0;
        void set (double hz, double q)
        {
            const double r = std::exp (-pi * hz / (q * rate));
            a1 = -2.0 * r * std::cos (2.0 * pi * hz / rate);
            a2 = r * r;
            b0 = 1.0 - r;
        }
        double process (double x)
        {
            const double y = b0 * x - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    bool save (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
    {
        std::string error;
        if (! wav::write (path, l.data(), r.data(), static_cast<long long> (l.size()), rate, error))
        {
            std::fprintf (stderr, "error: %s\n", error.c_str());
            return false;
        }
        std::printf ("wrote %s\n", path.c_str());
        return true;
    }

    /** A bowed string on C3: 40 partials with a body's formants, a slow bow attack, vibrato that
        arrives after the attack, and bow noise that follows the bow's pressure. */
    void bowed (std::vector<float>& out)
    {
        const double f0 = 130.8128, seconds = 4.5;
        const auto n = static_cast<std::size_t> (seconds * rate);
        out.assign (n, 0.0f);
        osp::Prng random (0xB0D1ED);
        const double formants[] = { 290.0, 470.0, 1150.0, 2600.0 };
        const double gains[] = { 1.0, 0.8, 0.45, 0.25 };
        std::vector<double> amp (40), phase (40);
        for (int h = 1; h <= 40; ++h)
        {
            const double f = f0 * h;
            double body = 0.15;
            for (int k = 0; k < 4; ++k)
                body += gains[k] / (1.0 + std::pow ((f - formants[k]) / (0.25 * formants[k]), 2.0));
            amp[static_cast<std::size_t> (h - 1)] = body / std::pow (h, 0.9);
            phase[static_cast<std::size_t> (h - 1)] = random.nextDouble() * 2.0 * pi;
        }
        Resonator noiseBody;
        noiseBody.set (1800.0, 1.2);
        double vibPhase = 0.0, sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double attack = std::min (1.0, t / 0.35);
            const double release = std::clamp ((seconds - t) / 0.5, 0.0, 1.0);
            const double bow = attack * attack * (3.0 - 2.0 * attack) * release * (0.92 + 0.08 * std::sin (2.0 * pi * 0.7 * t));
            const double vibDepth = std::clamp ((t - 0.7) / 0.8, 0.0, 1.0) * 0.006;
            vibPhase += 2.0 * pi * 5.4 / rate;
            const double ratio = 1.0 + vibDepth * std::sin (vibPhase);
            double x = 0.0;
            for (std::size_t h = 0; h < amp.size(); ++h)
            {
                const double f = f0 * static_cast<double> (h + 1) * ratio;
                if (f > 0.45 * rate)
                    break;
                phase[h] += 2.0 * pi * f / rate;
                x += amp[h] * std::sin (phase[h]);
            }
            const double noise = noiseBody.process (random.bipolar()) * 2.5;
            out[i] = static_cast<float> (0.28 * bow * (x + noise * (0.15 + 0.35 * (1.0 - attack))));
            sum += std::abs (out[i]);
        }
        (void) sum;
    }

    /** Two bars at 120 BPM: a tick on every sixteenth, an accent on each beat, a low "tock" on
        the bar. Ends exactly on the loop point. */
    void clockwork (std::vector<float>& l, std::vector<float>& r)
    {
        const double seconds = 4.0;   // 2 bars of 4/4 at 120 BPM
        const auto n = static_cast<std::size_t> (seconds * rate);
        l.assign (n, 0.0f);
        r.assign (n, 0.0f);
        osp::Prng random (0xC10C);
        const double sixteenth = 0.125;
        for (int k = 0; k < 32; ++k)
        {
            const bool beat = (k % 4) == 0, bar = (k % 16) == 0;
            const double level = bar ? 0.9 : (beat ? 0.6 : 0.28 + 0.06 * random.nextDouble());
            const auto start = static_cast<std::size_t> (k * sixteenth * rate);
            Resonator a, b, low;
            a.set (2300.0 + 200.0 * random.bipolar(), 18.0);
            b.set (3700.0, 22.0);
            low.set (620.0, 8.0);
            const double pan = (k % 2 == 0) ? -0.25 : 0.25;
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.09 * rate) && start + i < n; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                const double burst = i < 48 ? random.bipolar() * (1.0 - static_cast<double> (i) / 48.0) : 0.0;
                double x = (a.process (burst) * 6.0 + b.process (burst) * 4.0) * std::exp (-t * 70.0);
                if (bar || beat)
                    x += low.process (burst) * (bar ? 5.0 : 2.0) * std::exp (-t * 30.0);
                x *= level;
                l[start + i] += static_cast<float> (x * (1.0 - pan));
                r[start + i] += static_cast<float> (x * (1.0 + pan));
            }
        }
    }

    /** Rain: a pink bed and many droplets (short resonant pings), stereo. */
    void rain (std::vector<float>& l, std::vector<float>& r)
    {
        const double seconds = 6.0;
        const auto n = static_cast<std::size_t> (seconds * rate);
        l.assign (n, 0.0f);
        r.assign (n, 0.0f);
        osp::Prng random (0x7A1);
        double b0 = 0, b1 = 0, b2 = 0, c0 = 0, c1 = 0, c2 = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double w1 = random.bipolar(), w2 = random.bipolar();
            b0 = 0.99765 * b0 + w1 * 0.0990460;
            b1 = 0.96300 * b1 + w1 * 0.2965164;
            b2 = 0.57000 * b2 + w1 * 1.0526913;
            c0 = 0.99765 * c0 + w2 * 0.0990460;
            c1 = 0.96300 * c1 + w2 * 0.2965164;
            c2 = 0.57000 * c2 + w2 * 1.0526913;
            l[i] = static_cast<float> ((b0 + b1 + b2) * 0.02);
            r[i] = static_cast<float> ((c0 + c1 + c2) * 0.02);
        }
        for (int d = 0; d < 900; ++d)
        {
            const auto start = static_cast<std::size_t> (random.nextDouble() * (seconds - 0.05) * rate);
            Resonator ping;
            ping.set (1800.0 + 5000.0 * random.nextDouble(), 12.0 + 20.0 * random.nextDouble());
            const double level = 0.15 * std::pow (random.nextDouble(), 2.0) + 0.02;
            const double pan = random.bipolar();
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.04 * rate) && start + i < n; ++i)
            {
                const double x = ping.process (i < 4 ? random.bipolar() * 4.0 : 0.0) * level * 8.0;
                l[start + i] += static_cast<float> (x * (1.0 - 0.6 * pan));
                r[start + i] += static_cast<float> (x * (1.0 + 0.6 * pan));
            }
        }
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::puts ("make-samples OUTDIR");
        return 1;
    }
    const std::string dir = argv[1];
    std::vector<float> a, b;
    bowed (a);
    if (! save (dir + "/bowed-c3.wav", a, a))
        return 3;
    clockwork (a, b);
    if (! save (dir + "/clockwork-120.wav", a, b))
        return 3;
    rain (a, b);
    if (! save (dir + "/rain-texture.wav", a, b))
        return 3;
    return 0;
}
