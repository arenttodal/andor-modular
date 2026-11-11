// EXTERNAL INSTRUMENT through the kernel, with a surrogate instrument standing in for a hosted
// plugin (Kontakt strings): it is one global instance, hears every note with its overlaps in
// order, feeds GLOBAL MORPH, survives patch edits, and its absence is graceful.

#include "TestUtil.h"

#include "graph/External.h"

#include <cmath>
#include <mutex>

using namespace andor::modular;
using test::reg;

namespace
{
    /** A small polyphonic sine "string section" that logs what it hears. */
    class Surrogate final : public ExternalInstrument
    {
    public:
        explicit Surrogate (bool present) : loaded (present) {}
        std::string name() const override { return "Surrogate Strings"; }
        bool isLoaded() const noexcept override { return loaded; }
        void prepare (double sampleRate, int) override
        {
            rate = sampleRate;
            ++prepares;
        }
        void render (const NoteEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept override
        {
            int e = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                while (e < numEvents && events[e].offset <= i)
                {
                    const auto& ev = events[e++];
                    if (log.size() < log.capacity())
                        log.push_back ({ ev.kind == NoteEvent::Kind::noteOn, ev.note });
                    if (ev.kind == NoteEvent::Kind::noteOn)
                    {
                        notes[ev.note & 127] = 1.0f;
                        ++noteOns;
                    }
                    else
                        notes[ev.note & 127] = 0.0f;
                }
                float x = 0.0f;
                for (int n = 0; n < 128; ++n)
                {
                    level[static_cast<std::size_t> (n)] += 0.001f * (notes[static_cast<std::size_t> (n)] - level[static_cast<std::size_t> (n)]);
                    if (level[static_cast<std::size_t> (n)] > 1.0e-4f)
                    {
                        phase[static_cast<std::size_t> (n)] += 440.0 * std::exp2 ((n - 69) / 12.0) / rate;
                        x += 0.15f * level[static_cast<std::size_t> (n)] * static_cast<float> (std::sin (6.283185307179586 * phase[static_cast<std::size_t> (n)]));
                    }
                }
                left[i] = right[i] = x;
            }
        }
        bool loaded = true;
        double rate = 48000.0;
        int prepares = 0, noteOns = 0;
        std::array<float, 128> notes {}, level {};
        std::array<double, 128> phase {};
        struct Heard
        {
            bool on;
            int note;
        };
        std::vector<Heard> log = [] {
            std::vector<Heard> v;
            v.reserve (4096);
            return v;
        }();
    };

    Document externalPatch (bool withMorph)
    {
        Document d;
        const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
        (void) osc;
        auto* ext = d.addNode (reg(), "src.external", 0, 300);
        ext->strings["plugin"] = "/Library/Audio/Plug-Ins/VST3/Strings.vst3";
        const auto id = ext->id;
        d.connect (test::nodeOfType (d, "event.keyboard"), "notes", id, "notes");
        if (withMorph)
        {
            const auto mix = test::nodeOfType (d, "util.voicemix");
            std::string cable;
            for (const auto& c : d.connections)
                if (c.from == mix)
                    cable = c.id;
            const auto gm = edit::insertOnConnection (d, reg(), cable, "proc.morph.global");
            REQUIRE (gm.ok);
            REQUIRE (edit::connect (d, reg(), id, "out", gm.focus, "b").ok);
        }
        else
        {
            std::vector<std::string> made;
            std::string note;
            REQUIRE_FALSE (edit::routeToOutput (d, reg(), id, "out", made, note).empty());
        }
        return d;
    }
}

TEST_CASE ("EXTERNAL INSTRUMENT: one global instance hears every note, overlaps in order", "[integration][external]")
{
    auto surrogate = std::make_shared<Surrogate> (true);
    Document d = externalPatch (false);
    d.findNode (test::nodeOfType (d, "osc.analog"))->enabled = false;   // the plugin alone
    d.settings.polyphony = 8;
    RenderSettings s;
    s.seconds = 1.5;
    // a legato phrase: each note starts before the previous ends
    s.notes = { { 60, 0.0, 0.55 }, { 62, 0.5, 0.55 }, { 64, 1.0, 0.45 } };
    OfflineRenderer r;
    CompileOptions unused;
    (void) unused;
    setExternalFactory ([&] (const std::string&) { return surrogate; });
    const auto out = r.render (d, s);
    setExternalFactory ({});
    CHECK (test::rms (out.left, 4800) > 0.01);
    CHECK (surrogate->noteOns == 3);   // never multiplied by the polyphony
    // the overlap reached it in order: 60 on, 62 on, 60 off, 64 on, 62 off, ...
    REQUIRE (surrogate->log.size() >= 5);
    CHECK ((surrogate->log[0].on && surrogate->log[0].note == 60));
    CHECK ((surrogate->log[1].on && surrogate->log[1].note == 62));
    CHECK ((! surrogate->log[2].on && surrogate->log[2].note == 60));
    CHECK ((surrogate->log[3].on && surrogate->log[3].note == 64));
}

TEST_CASE ("EXTERNAL INSTRUMENT into GLOBAL MORPH; absent plugin is silent but harmless", "[integration][external]")
{
    auto surrogate = std::make_shared<Surrogate> (true);
    Document d = externalPatch (true);
    RenderSettings s;
    s.seconds = 1.0;
    s.notes = { { 48, 0.0, 1.0 }, { 55, 0.0, 1.0 } };
    setExternalFactory ([&] (const std::string&) { return surrogate; });
    OfflineRenderer withPlugin;
    const auto morphed = withPlugin.render (d, s);
    setExternalFactory ({});
    // the carrier alone (the plugin absent): GLOBAL MORPH hands A through
    auto absent = std::make_shared<Surrogate> (false);
    setExternalFactory ([&] (const std::string&) { return absent; });
    OfflineRenderer without;
    const auto carrier = without.render (d, s);
    setExternalFactory ({});
    CHECK (test::finite (morphed.left));
    CHECK (test::rms (carrier.left, 9600) > 0.01);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 9600; i < carrier.left.size(); ++i)
    {
        diff += std::pow (morphed.left[i] - carrier.left[i], 2.0);
        ref += std::pow (carrier.left[i], 2.0);
    }
    CHECK (diff / ref > 0.05);   // the plugin's sound reached the morph
    CHECK (absent->noteOns == 0);
    // the module and its strings survive save / load while the plugin is missing
    Document back;
    REQUIRE (loadFromString (saveToString (d), back).ok);
    CHECK (back.findNode (test::nodeOfType (back, "src.external"))->strings.at ("plugin") == "/Library/Audio/Plug-Ins/VST3/Strings.vst3");
}

TEST_CASE ("EXTERNAL INSTRUMENT survives patch edits (the same instance, prepared once)", "[integration][external]")
{
    auto surrogate = std::make_shared<Surrogate> (true);
    Document d = externalPatch (false);
    setExternalFactory ([&] (const std::string&) { return surrogate; });
    Engine e;
    e.prepare (48000.0, 256);
    CompileOptions o;
    e.publish (compileGraph (d, reg(), o));
    edit::addModule (d, reg(), "proc.filter", 100, 0, test::nodeOfType (d, "osc.analog"));
    e.publish (compileGraph (d, reg(), o));
    std::vector<float> l (256), r (256);
    osp::HostTiming t;
    MidiMessage on { 0, MidiMessage::Kind::noteOn, 1, 60, 0.8f, 0 };
    e.process (l.data(), r.data(), 256, &on, 1, t);
    for (int b = 0; b < 20; ++b)
        e.process (l.data(), r.data(), 256, nullptr, 0, t);
    setExternalFactory ({});
    CHECK (surrogate->noteOns == 1);   // during the swap only the newest graph played it
}
