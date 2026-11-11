#include "TestUtil.h"

#include "core/Prng.h"

using namespace andor::modular;
using test::reg;

namespace
{
    AssetLibrary& library()
    {
        static AssetLibrary lib;
        return lib;
    }
    AssetResolver factoryAssets()
    {
        return [] (const std::string& key) { return library().loadNow (test::presetPath ("factory/" + key)); };
    }
}

TEST_CASE ("Every module processes random settings without NaN, in every domain it supports", "[unit][nodes][fuzz]")
{
    osp::Prng r (7);
    for (const auto* desc : reg().all())
    {
        if (! desc->available || desc->role == NodeRole::output || desc->role == NodeRole::event || desc->role == NodeRole::boundary)
            continue;
        for (const Domain domain : { Domain::voice, Domain::global })
        {
            if (! desc->supports (domain))
                continue;
            INFO (desc->type << " in " << domainName (domain));
            // KEYBOARD -> OSC -> [module] -> AMP ENV / VOICE MIX -> OUTPUT, or on the global bus.
            Document d;
            const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
            auto* n = d.addNode (reg(), desc->type, 300, 300);
            n->domain = domain;
            const auto id = n->id;
            n->strings["sample"] = "samples/bowed-c3.wav";
            for (const auto& p : desc->params)
                if (! p.structural)
                    n->params[p.id] = p.denormalise (r.nextDouble());
            const auto vca = test::nodeOfType (d, "proc.vca");
            const auto mix = test::nodeOfType (d, "util.voicemix");
            const auto out = test::nodeOfType (d, "util.output");
            if (! desc->inputs.empty() && isAudio (desc->inputs[0].kind) && ! desc->inputs[0].voiceSide)
            {
                const auto from = domain == Domain::voice ? osc : mix;
                const auto to = domain == Domain::voice ? vca : out;
                const auto cable = d.connections;
                for (const auto& c : cable)
                    if (c.from == from && c.to == to)
                        d.disconnect (c.id);
                (void) edit::connect (d, reg(), from, domain == Domain::voice ? "out" : "out", id, desc->inputs[0].id);
                if (! desc->outputs.empty())
                    (void) edit::connect (d, reg(), id, desc->outputs[0].id, to, "in");
            }
            else if (desc->role == NodeRole::source && ! desc->outputs.empty() && isAudio (desc->outputs[0].kind))
            {
                if (domain == Domain::voice)
                    (void) edit::connect (d, reg(), id, desc->outputs[0].id, mix, "in");
            }
            else if (! desc->outputs.empty() && isControlLike (desc->outputs[0].kind))
                (void) edit::modulate (d, reg(), id, desc->outputs[0].id, osc, "fine", 0.5);
            if (desc->inputIndex ("notes") >= 0)
                d.connect (test::nodeOfType (d, "event.keyboard"), "notes", id, "notes");
            OfflineRenderer renderer;
            const auto res = test::render (renderer, d, { { 48, 0.0, 0.4, 1.0f }, { 72, 0.1, 0.4, 0.2f } }, 0.7, 128, 48000.0, factoryAssets());
            CHECK (test::finite (res.left));
            CHECK (test::finite (res.right));
            CHECK (res.peak <= 1.0);
        }
    }
}

TEST_CASE ("FILTER: the ladder is a low-pass, HIGH 12 a high-pass", "[unit][nodes][dsp]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, osc).focus;
    edit::setParam (d, reg(), filter, "cutoff", 500.0);
    edit::setParam (d, reg(), filter, "envAmount", 0.0);
    edit::setParam (d, reg(), filter, "keytrack", 0.0);
    edit::setParam (d, reg(), filter, "resonance", 0.0);
    OfflineRenderer r1;
    const auto low = test::render (r1, d, { { 45, 0.0, 1.0 } }, 0.6);
    edit::setParam (d, reg(), filter, "mode", 5);
    OfflineRenderer r2;
    const auto high = test::render (r2, d, { { 45, 0.0, 1.0 } }, 0.6);
    const double lowAt110 = test::toneLevel (low.left, 48000.0, 110.0, 9600, 14400);
    const double lowAt3520 = test::toneLevel (low.left, 48000.0, 3520.0, 9600, 14400);
    const double highAt110 = test::toneLevel (high.left, 48000.0, 110.0, 9600, 14400);
    const double highAt3520 = test::toneLevel (high.left, 48000.0, 3520.0, 9600, 14400);
    CHECK (lowAt110 > 100.0 * lowAt3520);    // a saw's 32nd partial is only 30 dB down: the ladder takes it far lower
    // relative to the low-pass, the high-pass favours the top by a wide margin
    CHECK (highAt3520 / highAt110 > 1000.0 * (lowAt3520 / lowAt110));
}

TEST_CASE ("MORPH: the ends are the sources; the middle is a fusion, not a crossfade", "[unit][nodes][dsp][morph]")
{
    Document d;
    const auto a = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    const auto b = edit::addModule (d, reg(), "osc.analog", 0, 200).focus;
    edit::setParam (d, reg(), a, "drift", 0.0);
    edit::setParam (d, reg(), b, "drift", 0.0);
    edit::setParam (d, reg(), b, "wave", 2);       // pulse
    edit::setParam (d, reg(), b, "width", 0.2);
    edit::setParam (d, reg(), a, "phaseReset", 1.0);
    edit::setParam (d, reg(), b, "phaseReset", 1.0);
    const auto m = edit::dropOnSource (d, reg(), b, a, edit::DropAction::morph);
    REQUIRE (m.ok);
    edit::setParam (d, reg(), m.focus, "levelMatch", 0.0);
    edit::setParam (d, reg(), m.focus, "attack", 0.0);

    auto renderAt = [&] (double position) {
        edit::setParam (d, reg(), m.focus, "position", position);
        OfflineRenderer r;
        return test::render (r, d, { { 57, 0.0, 1.2 } }, 1.0).left;
    };
    // A alone and B alone (the same chain with the other source muted) for reference.
    auto solo = [&] (const std::string& mute) {
        Document s = d;
        s.findNode (mute)->enabled = false;
        edit::setParam (s, reg(), m.focus, "position", mute == b ? 0.0 : 1.0);
        OfflineRenderer r;
        return test::render (r, s, { { 57, 0.0, 1.2 } }, 1.0).left;
    };
    const auto at0 = renderAt (0.0), at1 = renderAt (1.0), mid = renderAt (0.5);
    const auto onlyA = solo (b), onlyB = solo (a);
    double e0 = 0.0, e1 = 0.0, ref = 0.0;
    for (std::size_t i = 4800; i < 40000; ++i)
    {
        e0 += std::pow (at0[i] - onlyA[i], 2.0);
        e1 += std::pow (at1[i] - onlyB[i], 2.0);
        ref += std::pow (onlyA[i], 2.0);
    }
    CHECK (e0 / ref < 1.0e-6);   // 0 %: the carrier itself
    CHECK (e1 / ref < 1.0e-6);   // 100 %: the source itself
    // 50 %: far from the linear crossfade of the two
    double diff = 0.0, mref = 0.0;
    for (std::size_t i = 9600; i < 40000; ++i)
    {
        const double cross = 0.5 * (onlyA[i] + onlyB[i]);
        diff += std::pow (mid[i] - cross, 2.0);
        mref += cross * cross;
    }
    CHECK (diff / mref > 0.1);
    CHECK (test::rms (mid, 9600, 40000) > 0.01);
}

TEST_CASE ("GLOBAL MORPH: exact ends (delayed by its frame), imprint in between", "[unit][nodes][dsp][morph]")
{
    Document d = test::basicPatch();
    const auto mix = test::nodeOfType (d, "util.voicemix");
    const auto out = test::nodeOfType (d, "util.output");
    const auto gm = edit::insertOnConnection (d, reg(), d.connections.back().id, "proc.morph.global");
    REQUIRE (gm.ok);
    auto* noise = d.addNode (reg(), "src.noise", 0, 300);
    noise->domain = Domain::global;
    const auto nid = noise->id;
    REQUIRE (edit::connect (d, reg(), nid, "out", gm.focus, "b").ok);
    edit::setParam (d, reg(), gm.focus, "levelMatch", 0.0);
    (void) mix;
    (void) out;
    auto renderAt = [&] (double p) {
        edit::setParam (d, reg(), gm.focus, "position", p);
        OfflineRenderer r;
        return test::render (r, d, { { 57, 0.0, 1.0 } }, 1.0);
    };
    Document dry = d;
    dry.findNode (gm.focus)->enabled = false;   // bypassed: A undelayed
    OfflineRenderer rd;
    const auto a = test::render (rd, dry, { { 57, 0.0, 1.0 } }, 1.0);
    const auto zero = renderAt (0.0);
    CHECK (zero.graph->latencySamples == 2048);
    double e = 0.0, ref = 0.0;
    for (std::size_t i = 4096; i < 40000; ++i)
    {
        e += std::pow (zero.left[i] - a.left[i - 2048], 2.0);
        ref += std::pow (a.left[i - 2048], 2.0);
    }
    CHECK (e / ref < 1.0e-8);
    const auto half = renderAt (0.5);
    CHECK (test::finite (half.left));
    CHECK (test::rms (half.left, 9600, 40000) > 0.001);
}

TEST_CASE ("SATELLITE: it moves, and its mono downmix stays coherent", "[unit][nodes][dsp][spatial]")
{
    Document d = test::basicPatch();
    const auto sat = edit::insertOnConnection (d, reg(), d.connections.back().id, "spatial.satellite");
    REQUIRE (sat.ok);
    edit::setParam (d, reg(), sat.focus, "rate", 2.0);
    edit::setParam (d, reg(), sat.focus, "depth", 1.0);
    for (const int mode : { 0, 1, 2 })
    {
        edit::setParam (d, reg(), sat.focus, "mode", mode);
        OfflineRenderer r;
        const auto res = test::render (r, d, { { 57, 0.0, 2.0 } }, 2.0);
        // L/R balance swings
        double minBal = 1.0, maxBal = -1.0, minMono = 1e9, maxMono = 0.0;
        for (std::size_t w = 4800; w + 2400 < 96000; w += 2400)
        {
            const double l = test::rms (res.left, w, w + 2400), rr = test::rms (res.right, w, w + 2400);
            const double bal = (rr - l) / (rr + l + 1e-9);
            minBal = std::min (minBal, bal);
            maxBal = std::max (maxBal, bal);
            std::vector<float> mono (2400);
            for (std::size_t i = 0; i < 2400; ++i)
                mono[i] = 0.5f * (res.left[w + i] + res.right[w + i]);
            const double m = test::rms (mono);
            minMono = std::min (minMono, m);
            maxMono = std::max (maxMono, m);
        }
        INFO ("mode " << mode);
        CHECK (maxBal - minBal > 0.8);
        // equal-power panning: the mono sum varies by at most 3 dB (+ ORBIT's "behind" 6 dB)
        CHECK (20.0 * std::log10 (maxMono / minMono) < (mode == 1 ? 9.5 : 3.5));
    }
}

TEST_CASE ("ARP: patterned notes from a held chord on the host grid", "[unit][nodes][events]")
{
    Document d;
    const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    const auto arp = edit::addModule (d, reg(), "event.arp", -150, 0, osc).focus;
    edit::setParam (d, reg(), arp, "rate", 2);   // 1/16 at 120 BPM: 8 steps a second
    edit::setParam (d, reg(), test::nodeOfType (d, "proc.vca"), "release", 0.01);
    OfflineRenderer r;
    const auto res = test::render (r, d, { { 60, 0.0, 2.0 }, { 64, 0.0, 2.0 }, { 67, 0.0, 2.0 } }, 2.0);
    // One voice at a time (arp steps), and the steps change pitch.
    CHECK (res.maxVoices <= 2);
    const double f1 = test::fundamental (res.left, 48000.0, 1000, 2400);
    const double f2 = test::fundamental (res.left, 48000.0, 1000 + 6000, 2400);
    CHECK (std::abs (f1 - f2) > 10.0);
}

TEST_CASE ("GENERATIVE: the same seed plays the same notes; another variation does not", "[unit][nodes][events][determinism]")
{
    Document d;
    const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    auto* g = d.addNode (reg(), "event.generative", -300, 0);
    const auto gen = g->id;
    d.disconnect (d.connections.front().id);
    d.connect (gen, "notes", osc, "notes");
    OfflineRenderer r1, r2, r3;
    const auto a = test::render (r1, d, {}, 3.0);
    const auto b = test::render (r2, d, {}, 3.0);
    CHECK (a.left == b.left);
    CHECK (test::rms (a.left) > 0.01);
    edit::setParam (d, reg(), gen, "variation", 7);
    const auto c = test::render (r3, d, {}, 3.0);
    CHECK (c.left != a.left);
}

TEST_CASE ("ORGANIC SAMPLE: a recording plays across the keyboard at pitch", "[integration][nodes][organic]")
{
    Document d;
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
    d.findNode (org)->strings["sample"] = "samples/bowed-c3.wav";
    auto asset = library().loadNow (test::presetPath ("factory/samples/bowed-c3.wav"));
    REQUIRE (asset != nullptr);
    REQUIRE (asset->status == SampleAsset::Status::ready);
    CHECK (std::abs (asset->rootMidi - 48.0) < 0.5);   // C3 found by OSP's analysis
    for (const int note : { 48, 55, 60 })
    {
        OfflineRenderer r;
        const auto res = test::render (r, d, { { note, 0.0, 2.0 } }, 1.5, 256, 48000.0, factoryAssets());
        const double f = test::fundamental (res.left, 48000.0, 24000, 9600, 60.0, 800.0);
        const double expected = 440.0 * std::exp2 ((note - 69) / 12.0);
        INFO ("note " << note << " measured " << f);
        CHECK (std::abs (1200.0 * std::log2 (f / expected)) < 20.0);   // the recording's own vibrato is +-10 cents
    }
    // a missing file: the module stays, silent, its key kept
    auto missing = library().loadNow ("/nowhere/lost.wav");
    CHECK (missing->status == SampleAsset::Status::missing);
}

TEST_CASE ("RHYTHMIC SOURCE: the clock loop's tempo is found; its envelope drives a filter", "[integration][nodes][rhythm]")
{
    auto asset = library().loadNow (test::presetPath ("factory/samples/clockwork-120.wav"));
    REQUIRE (asset != nullptr);
    CHECK (std::abs (asset->tempoBpm - 120.0) < 2.0);
    CHECK (asset->beats == Catch::Approx (8.0));
    CHECK (asset->onsetsSeconds.size() >= 16);

    Document d;
    REQUIRE (loadFromFile (test::presetPath ("factory/clockwork.andormodular"), d).ok);
    OfflineRenderer r;
    const auto res = test::render (r, d, { { 48, 0.0, 4.0 } }, 4.0, 256, 48000.0, factoryAssets());
    REQUIRE (test::finite (res.left));
    // The pad's level pulses with the ticks: 16th-note energy modulation.
    std::vector<double> env;
    for (std::size_t w = 48000; w + 600 < 192000; w += 600)
        env.push_back (test::rms (res.left, w, w + 600));
    double lo = 1e9, hi = 0.0;
    for (const double e : env)
    {
        lo = std::min (lo, e);
        hi = std::max (hi, e);
    }
    CHECK (hi > 2.0 * lo);
}

TEST_CASE ("STEP SEQUENCER: steps land on the host grid", "[unit][nodes][events]")
{
    Document d;
    const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    auto* s = d.addNode (reg(), "event.stepseq", -300, 0);
    const auto seq = s->id;
    d.disconnect (d.connections.front().id);
    d.connect (seq, "notes", osc, "notes");
    edit::setParam (d, reg(), seq, "rate", 0);   // quarter notes at 120 BPM: every 24000 samples
    edit::setParam (d, reg(), seq, "gate", 0.25);
    const auto vca = test::nodeOfType (d, "proc.vca");
    edit::setParam (d, reg(), vca, "attack", 0.001);
    edit::setParam (d, reg(), vca, "release", 0.01);
    OfflineRenderer r;
    const auto res = test::render (r, d, {}, 2.0);
    for (const std::size_t beat : { 24000u, 48000u, 72000u })
    {
        INFO ("beat at " << beat);
        CHECK (test::rms (res.left, beat - 600, beat - 100) < 0.002);   // silence before the step
        CHECK (test::rms (res.left, beat + 100, beat + 600) > 0.02);    // sound right after
    }
}
