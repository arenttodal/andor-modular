// Real-time safety: Engine::process() must not touch the heap, for any factory patch, while
// notes start, steal and end, while macros and scenes move, and while graphs are swapped.

#include "TestUtil.h"

#include "edit/Factory.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<bool> watching { false };
    std::atomic<long> heapCalls { 0 };
}

void* operator new (std::size_t size)
{
    if (watching.load (std::memory_order_relaxed))
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)
{
    if (watching.load (std::memory_order_relaxed))
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept
{
    if (watching.load (std::memory_order_relaxed) && p != nullptr)
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept
{
    if (watching.load (std::memory_order_relaxed) && p != nullptr)
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace andor::modular;
using test::reg;

TEST_CASE ("process() never allocates or frees: every factory patch, notes, macros, swaps", "[realtime]")
{
    AssetLibrary assets;
    const auto folder = test::presetPath ("factory/");
    auto resolver = [&] (const std::string& key) { return assets.loadNow (folder + key); };
    for (auto doc : factory::patches (reg()))
    {
        INFO (doc.name);
        for (const int block : { 32, 333, 512 })
        {
            Engine e;
            e.prepare (48000.0, 512);
            CompileOptions o;
            o.maxBlock = 512;
            o.assets = resolver;
            e.publish (compileGraph (doc, reg(), o));
            std::vector<float> l (512), r (512);
            osp::HostTiming t;
            t.valid = t.playing = true;
            t.bpm = 128.0;
            long calls = 0;
            for (int b = 0; b < 160; ++b)
            {
                std::vector<MidiMessage> midi;
                if (b % 20 == 0)
                    for (int k = 0; k < 10; ++k)   // more notes than voices: stealing
                        midi.push_back ({ k * 3 % block, MidiMessage::Kind::noteOn, 1, static_cast<std::uint8_t> (40 + 4 * k), 0.9f, 0 });
                if (b % 20 == 12)
                    for (int k = 0; k < 10; ++k)
                        midi.push_back ({ 0, MidiMessage::Kind::noteOff, 1, static_cast<std::uint8_t> (40 + 4 * k), 0.0f, 0 });
                if (b == 70)
                    midi.push_back ({ 0, MidiMessage::Kind::pitchBend, 1, 0, 0.7f, 0 });
                if (b == 50 || b == 51 || b == 90)
                {
                    // a patch edit: compiled on this (message) thread, swapped on the next block
                    doc.macros[0].value = 0.9;
                    e.publish (compileGraph (doc, reg(), o));
                }
                e.setMacro (0, static_cast<float> (b % 37) / 37.0f);
                e.setMacro (1, static_cast<float> (b % 11) / 11.0f);
                e.setScenePosition (static_cast<float> (b % 40) / 13.0f);
                t.ppq += block / 48000.0 * 128.0 / 60.0;
                heapCalls = 0;
                watching = true;
                e.process (l.data(), r.data(), block, midi.data(), static_cast<int> (midi.size()), t);
                watching = false;
                calls += heapCalls.load();
                e.collectGarbage();
                for (int i = 0; i < block; ++i)
                    REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
            }
            CHECK (calls == 0);
        }
    }
}
