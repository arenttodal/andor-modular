#include "TestUtil.h"

using namespace andor::modular;
using test::reg;

TEST_CASE ("0-to-sound: Quick Add ANALOG OSC plays the played pitch", "[integration][engine]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    edit::setParam (d, reg(), osc, "drift", 0.0);
    for (const int note : { 45, 57, 69, 81 })
    {
        OfflineRenderer r;
        const auto out = test::render (r, d, { { note, 0.0, 1.0 } }, 0.8);
        REQUIRE (test::finite (out.left));
        const double expected = 440.0 * std::exp2 ((note - 69) / 12.0);
        const double f = test::fundamental (out.left, 48000.0, 9600, 4800, 40.0, 2500.0);
        INFO ("note " << note << " measured " << f);
        CHECK (std::abs (1200.0 * std::log2 (f / expected)) < 3.0);   // within 3 cents
    }
}

TEST_CASE ("Polyphony, release and voice stealing", "[integration][engine]")
{
    Document d = test::basicPatch();
    d.settings.polyphony = 4;
    OfflineRenderer r;
    std::vector<ScoreNote> notes;
    for (int k = 0; k < 8; ++k)
        notes.push_back ({ 48 + 2 * k, 0.05 * k, 0.6 });
    const auto out = test::render (r, d, notes, 2.0);
    CHECK (test::finite (out.left));
    CHECK (out.maxVoices == 4);                                       // stolen, never exceeded
    CHECK (test::rms (out.left, 96000 - 4800, 96000) < 1.0e-4);       // all released
    CHECK (r.engine().activeVoices() == 0);
}

TEST_CASE ("Note ids: two streams playing one pitch never cut each other", "[integration][engine]")
{
    // KEYBOARD and a second source (an ARP) both play C4 into one lane via NOTE MERGE: the
    // keyboard's note-off must not end the arp's note (Merge policy: distinct ids).
    Document d;
    const auto osc = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    const auto keys = test::nodeOfType (d, "event.keyboard");
    auto* m = d.addNode (reg(), "event.merge", -150, 0);
    const auto merge = m->id;
    auto* gen = d.addNode (reg(), "event.stepseq", -300, 200);
    const auto seq = gen->id;
    edit::setParam (d, reg(), seq, "length", 1);
    edit::setParam (d, reg(), seq, "root", 60);
    edit::setParam (d, reg(), seq, "p1", 0);
    edit::setParam (d, reg(), seq, "rate", 0);   // quarters
    edit::setParam (d, reg(), seq, "gate", 1.5);   // overlapping: always a note sounding
    d.disconnect (d.connections.front().id);
    d.connect (keys, "notes", merge, "a");
    d.connect (seq, "notes", merge, "b");
    d.connect (merge, "notes", osc, "notes");
    OfflineRenderer r;
    const auto out = test::render (r, d, { { 60, 0.0, 0.3 } }, 2.0);
    // After the key is released the sequencer's C4 keeps sounding.
    CHECK (test::rms (out.left, 48000, 96000) > 0.01);
}

TEST_CASE ("Sustain pedal holds; MONO LEGATO glides without retriggering", "[integration][engine]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    edit::setParam (d, reg(), osc, "drift", 0.0);
    Engine e;
    e.prepare (48000.0, 256);
    CompileOptions o;
    e.publish (compileGraph (d, reg(), o));
    std::vector<float> l (256), rr (256);
    osp::HostTiming t;
    MidiMessage on { 0, MidiMessage::Kind::noteOn, 1, 60, 0.8f, 0 };
    MidiMessage ped { 0, MidiMessage::Kind::sustain, 1, 0, 1.0f, 64 };
    MidiMessage off { 10, MidiMessage::Kind::noteOff, 1, 60, 0.0f, 0 };
    MidiMessage pedUp { 0, MidiMessage::Kind::sustain, 1, 0, 0.0f, 64 };
    MidiMessage block1[] = { on, ped, off };
    e.process (l.data(), rr.data(), 256, block1, 3, t);
    for (int b = 0; b < 40; ++b)
        e.process (l.data(), rr.data(), 256, nullptr, 0, t);
    CHECK (e.activeVoices() == 1);   // held by the pedal
    e.process (l.data(), rr.data(), 256, &pedUp, 1, t);
    for (int b = 0; b < 200; ++b)
        e.process (l.data(), rr.data(), 256, nullptr, 0, t);
    CHECK (e.activeVoices() == 0);

    // LEGATO: a second key while the first is held glides (one voice, envelope not restarted).
    d.settings.voiceMode = 2;
    d.settings.glideSeconds = 0.05;
    OfflineRenderer r;
    const auto out = test::render (r, d, { { 57, 0.0, 1.0 }, { 69, 0.5, 0.5 } }, 1.0);
    CHECK (out.maxVoices == 1);
    const double f = test::fundamental (out.left, 48000.0, 40000, 4800);
    CHECK (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0);
}

TEST_CASE ("Rendering is deterministic and block-size independent", "[integration][engine][determinism]")
{
    Document d = test::basicPatch();
    const auto lfo = edit::addModule (d, reg(), "ctl.random", -200, 200, test::nodeOfType (d, "osc.analog"));
    REQUIRE (lfo.ok);
    const std::vector<ScoreNote> notes { { 48, 0.0, 0.7 }, { 55, 0.1, 0.6 }, { 64, 0.3, 0.5 } };
    OfflineRenderer r1, r2, r3;
    const auto a = test::render (r1, d, notes, 1.0, 256);
    const auto b = test::render (r2, d, notes, 1.0, 256);
    CHECK (a.left == b.left);
    CHECK (a.right == b.right);
    // other host block sizes: sample for sample the same (the engine's fixed grid)
    for (const int block : { 1, 37, 64, 512, 1000 })
    {
        OfflineRenderer other;
        const auto c = test::render (other, d, notes, 1.0, block);
        INFO ("block " << block);
        CHECK (c.left == a.left);
        CHECK (c.right == a.right);
    }
    (void) r3;
}

TEST_CASE ("Graph hot swap under held notes: no click, no NaN, notes continue", "[integration][engine][realtime]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    edit::setParam (d, reg(), osc, "drift", 0.0);
    Engine e;
    e.prepare (48000.0, 128);
    CompileOptions o;
    o.maxBlock = 128;
    e.publish (compileGraph (d, reg(), o));
    std::vector<float> l (128), r (128), all;
    osp::HostTiming t;
    MidiMessage on { 0, MidiMessage::Kind::noteOn, 1, 57, 0.8f, 0 };
    e.process (l.data(), r.data(), 128, &on, 1, t);
    for (int b = 0; b < 100; ++b)
    {
        if (b == 30 || b == 31 || b == 60)   // rapid edits: a filter in, a level change, a swap mid-fade
        {
            if (b == 30)
                edit::addModule (d, reg(), "proc.filter", 200, 100, osc);
            if (b == 31)
                edit::setParam (d, reg(), osc, "level", 0.45);
            e.publish (compileGraph (d, reg(), o));
        }
        e.process (l.data(), r.data(), 128, nullptr, 0, t);
        all.insert (all.end(), l.begin(), l.end());
        e.collectGarbage();
    }
    CHECK (test::finite (all));
    CHECK (e.swapsCompleted() >= 2);
    CHECK (e.activeVoices() == 1);                       // the held note plays in the new graph
    CHECK (test::rms (all, all.size() - 2000, all.size()) > 0.02);
    // No click: the largest sample-to-sample step around the swaps is no larger than the saw's
    // own edges before them (blocks 5..29).
    auto maxStep = [&] (std::size_t from, std::size_t to) {
        double m = 0.0;
        for (std::size_t i = from + 1; i < to; ++i)
            m = std::max (m, static_cast<double> (std::abs (all[i] - all[i - 1])));
        return m;
    };
    CHECK (maxStep (29 * 128, 75 * 128) < 1.2 * maxStep (5 * 128, 29 * 128));
}

TEST_CASE ("Latency: a latent branch is aligned with its dry neighbour (null test)", "[integration][engine][latency]")
{
    // VOICE MIX feeds a MIXER twice: once dry, once through GLOBAL MORPH at 0 % (A exactly, 2048
    // samples late) and an inverting GAIN. Aligned, they cancel.
    Document d = test::basicPatch();
    const auto mix = test::nodeOfType (d, "util.voicemix");
    const auto out = test::nodeOfType (d, "util.output");
    d.disconnect (d.connections.back().id);   // voicemix -> output
    auto* mixer = d.addNode (reg(), "util.mixer", 600, 0);
    auto* gm = d.addNode (reg(), "proc.morph.global", 400, 200);
    auto* inv = d.addNode (reg(), "util.gain", 500, 200);
    const auto mixerId = mixer->id, gmId = gm->id, invId = inv->id;
    edit::setParam (d, reg(), gmId, "position", 0.0);
    edit::setParam (d, reg(), invId, "invert", 1.0);
    edit::setParam (d, reg(), mixerId, "level1", 0.0);
    edit::setParam (d, reg(), mixerId, "level2", 0.0);
    REQUIRE (edit::connect (d, reg(), mix, "out", mixerId, "in1").ok);
    REQUIRE (edit::connect (d, reg(), mix, "out", gmId, "a").ok);
    REQUIRE (edit::connect (d, reg(), gmId, "out", invId, "in").ok);
    REQUIRE (edit::connect (d, reg(), invId, "out", mixerId, "in2").ok);
    REQUIRE (edit::connect (d, reg(), mixerId, "out", out, "in").ok);
    OfflineRenderer r;
    const auto res = test::render (r, d, { { 60, 0.0, 1.0 } }, 1.2);
    CHECK (res.graph->latencySamples == 2048);
    CHECK (test::rms (res.left, 6000, 48000) < 1.0e-4);

    // Without the inversion the two add (no comb filter: the same signal, twice the level).
    edit::setParam (d, reg(), invId, "invert", 0.0);
    OfflineRenderer r2;
    const auto sum = test::render (r2, d, { { 60, 0.0, 1.0 } }, 1.2);
    CHECK (test::rms (sum.left, 6000, 48000) > 0.05);
}

TEST_CASE ("FEEDBACK DELAY closes a loop that rings but stays bounded", "[integration][engine]")
{
    Document d = test::basicPatch();
    const auto mix = test::nodeOfType (d, "util.voicemix");
    const auto out = test::nodeOfType (d, "util.output");
    d.disconnect (d.connections.back().id);
    auto* mixer = d.addNode (reg(), "util.mixer", 500, 0);
    auto* fb = d.addNode (reg(), "util.feedback", 500, 200);
    const auto mixerId = mixer->id, fbId = fb->id;
    edit::setParam (d, reg(), fbId, "feedback", 0.95);
    edit::setParam (d, reg(), mixerId, "level2", 6.0);   // a loop gain above one before the guard
    REQUIRE (edit::connect (d, reg(), mix, "out", mixerId, "in1").ok);
    REQUIRE (edit::connect (d, reg(), mixerId, "out", fbId, "in").ok);
    REQUIRE (edit::connect (d, reg(), fbId, "out", mixerId, "in2").ok);
    REQUIRE (edit::connect (d, reg(), mixerId, "out", out, "in").ok);
    Validator v (reg());
    CHECK_FALSE (v.validate (d).hasErrors());
    OfflineRenderer r;
    const auto res = test::render (r, d, { { 60, 0.0, 0.2 } }, 3.0);
    CHECK (test::finite (res.left));
    CHECK (res.peak <= 1.0);
    CHECK (test::rms (res.left, 48000, 96000) > 0.001);   // it rings after the note
}

TEST_CASE ("Macros, host automation and scenes move parameters", "[integration][engine]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, osc).focus;
    REQUIRE (edit::mapMacro (d, reg(), 0, filter, "cutoff", 0.0, 1.0));
    OfflineRenderer r;
    RenderSettings s;
    s.seconds = 0.5;
    s.notes = { { 48, 0.0, 0.5 } };
    s.setMacros = true;
    s.macros[0] = 0.05f;
    auto dark = r.render (d, s);
    const double darkHigh = test::toneLevel (dark.left, 48000.0, 3000.0, 12000, 9600);
    s.macros[0] = 0.95f;
    OfflineRenderer r2;
    auto bright = r2.render (d, s);
    const double brightHigh = test::toneLevel (bright.left, 48000.0, 3000.0, 12000, 9600);
    CHECK (brightHigh > 10.0 * darkHigh);

    // Host bank slot 3 bound to the cutoff.
    d.hostBindings[3] = { filter, "cutoff" };
    d.macros[0].mappings.clear();
    Engine e;
    e.prepare (48000.0, 256);
    CompileOptions o;
    e.publish (compileGraph (d, reg(), o));
    std::vector<float> l (256), rr (256);
    std::array<float, hostBankSize> host {};
    host.fill (-1.0f);
    host[3] = 0.0f;
    osp::HostTiming t;
    e.process (l.data(), rr.data(), 256, nullptr, 0, t, host.data());
    e.process (l.data(), rr.data(), 256, nullptr, 0, t, host.data());
    CHECK (e.latest()->getParam (filter, "cutoff") == Catch::Approx (20.0));
    CHECK (e.latest()->effectiveParam (filter, "cutoff") == Catch::Approx (20.0));

    // Scenes: a continuous position interpolates (log Hz in normalised space).
    edit::setParam (d, reg(), filter, "cutoff", 200.0);
    edit::captureScene (d, reg(), 0);
    edit::setParam (d, reg(), filter, "cutoff", 2000.0);
    edit::captureScene (d, reg(), 1);
    auto g = compileGraph (d, reg(), o);
    auto* raw = g.get();
    Engine e2;
    e2.prepare (48000.0, 256);
    e2.publish (std::move (g));
    e2.setScenePosition (0.5f);
    e2.process (l.data(), rr.data(), 256, nullptr, 0, t);
    e2.process (l.data(), rr.data(), 256, nullptr, 0, t);
    CHECK (raw->getParam (filter, "cutoff") == Catch::Approx (std::sqrt (200.0 * 2000.0)).epsilon (0.01));
    edit::applyScenePosition (d, reg(), 1.0);
    CHECK (d.findNode (filter)->params["cutoff"] == Catch::Approx (2000.0));
}
