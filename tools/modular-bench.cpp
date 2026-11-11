// modular-bench: the performance matrix (docs/PERFORMANCE_BUDGET.md). Renders representative
// patches at several rates, block sizes and polyphonies, timing every audio callback, and counts
// heap allocations made inside process() (there must be none).

#include "edit/Factory.h"
#include "graph/Compiler.h"
#include "graph/Engine.h"
#include "graph/Registry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

using namespace andor::modular;

namespace
{
    std::atomic<bool> counting { false };
    std::atomic<long> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::string (argv[1]) == "--quick";
    const std::vector<double> rates = quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 };
    const std::vector<int> blocks = quick ? std::vector<int> { 128 } : std::vector<int> { 32, 64, 128, 512 };
    const std::vector<int> voices = quick ? std::vector<int> { 8 } : std::vector<int> { 1, 4, 8, 16 };
    const auto& reg = Registry::builtIn();
    struct Case
    {
        const char* name;
        Document doc;
    };
    std::vector<Case> cases;
    for (const auto& p : factory::patches (reg))
        if (p.name == "Basic Poly" || p.name == "Three Arps" || p.name == "Six Oscillator Stack" || p.name == "Two-Morph Dream" || p.name == "Satellite Orchestra")
            cases.push_back ({ nullptr, p });
    std::printf ("| patch | rate | block | voices | p50 us | p95 us | p99 us | deadline us | p99 load %% | allocs in process |\n");
    std::printf ("|---|---|---|---|---|---|---|---|---|---|\n");
    for (auto& c : cases)
        for (const double rate : rates)
            for (const int block : blocks)
                for (const int v : voices)
                {
                    Document doc = c.doc;
                    doc.settings.polyphony = v;
                    Engine engine;
                    engine.prepare (rate, block);
                    CompileOptions o;
                    o.sampleRate = rate;
                    o.maxBlock = block;
                    engine.publish (compileGraph (doc, reg, o));
                    std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
                    std::vector<double> times;
                    const int blocksToRun = static_cast<int> (rate * (quick ? 2.0 : 3.0)) / block;
                    std::vector<MidiMessage> midi;
                    osp::HostTiming t;
                    t.valid = t.playing = true;
                    t.bpm = 120.0;
                    allocations = 0;
                    for (int b = 0; b < blocksToRun; ++b)
                    {
                        midi.clear();
                        // A chord of `v` notes held, re-struck every second.
                        const long long at = static_cast<long long> (b) * block;
                        const long long period = static_cast<long long> (rate);
                        if (at % period < block)
                            for (int k = 0; k < v; ++k)
                                midi.push_back ({ static_cast<int> (at % period), MidiMessage::Kind::noteOn, 1, static_cast<std::uint8_t> (48 + 3 * k), 0.8f, 0 });
                        if ((at + period / 2) % period < block)
                            for (int k = 0; k < v; ++k)
                                midi.push_back ({ static_cast<int> ((at + period / 2) % period), MidiMessage::Kind::noteOff, 1, static_cast<std::uint8_t> (48 + 3 * k), 0.0f, 0 });
                        t.ppq = static_cast<double> (at) / rate * 2.0;
                        counting = b > 2;   // the first blocks take the graph over
                        const auto s = std::chrono::steady_clock::now();
                        engine.process (l.data(), r.data(), block, midi.data(), static_cast<int> (midi.size()), t);
                        const auto e = std::chrono::steady_clock::now();
                        counting = false;
                        times.push_back (std::chrono::duration<double, std::micro> (e - s).count());
                        engine.collectGarbage();
                    }
                    std::sort (times.begin(), times.end());
                    auto pct = [&] (double q) { return times[static_cast<std::size_t> (q * static_cast<double> (times.size() - 1))]; };
                    const double deadline = 1.0e6 * block / rate;
                    std::printf ("| %s | %.0f | %d | %d | %.1f | %.1f | %.1f | %.0f | %.1f | %ld |\n", c.doc.name.c_str(), rate, block, v, pct (0.5), pct (0.95),
                                 pct (0.99), deadline, 100.0 * pct (0.99) / deadline, allocations.load());
                }
    return 0;
}
