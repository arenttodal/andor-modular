// Reliability: what a musician does while notes sound (a recording arriving, an octave
// change, a mute, a solo, a shared filter) must neither go silent, nor click, nor hang a note.

#include "TestUtil.h"

#include "core/Prng.h"
#include "edit/Factory.h"
#include "io/Wav.h"

using namespace test;

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

    /** An engine played block by block, with what it produced. */
    struct Player
    {
        explicit Player (int blockSize = 128, double sampleRate = 48000.0) : block (blockSize), rate (sampleRate)
        {
            engine.prepare (rate, block);
            options.sampleRate = rate;
            options.maxBlock = block;
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        void publish (const Document& d) { engine.publish (compileGraph (d, reg(), options)); }
        void run (int blocks, std::vector<MidiMessage> first = {})
        {
            for (int b = 0; b < blocks; ++b)
            {
                engine.process (l.data(), r.data(), block, b == 0 && ! first.empty() ? first.data() : nullptr, b == 0 ? static_cast<int> (first.size()) : 0, timing);
                left.insert (left.end(), l.begin(), l.end());
                right.insert (right.end(), r.begin(), r.end());
                engine.collectGarbage();
            }
        }
        static MidiMessage on (int note, float velocity = 0.8f) { return { 0, MidiMessage::Kind::noteOn, 1, static_cast<std::uint8_t> (note), velocity, 0 }; }
        static MidiMessage off (int note) { return { 0, MidiMessage::Kind::noteOff, 1, static_cast<std::uint8_t> (note), 0.0f, 0 }; }
        double tail (std::size_t samples) const { return test::rms (left, left.size() - samples, left.size()); }
        Engine engine;
        CompileOptions options;
        osp::HostTiming timing;
        int block;
        double rate;
        std::vector<float> l, r, left, right;
    };
}

namespace
{
    /** The largest sample-to-sample step in x[from, to). */
    double maxStep (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (std::size_t i = from + 1; i < to && i < x.size(); ++i)
            m = std::max (m, static_cast<double> (std::abs (x[i] - x[i - 1])));
        return m;
    }

    /** KEYBOARD -> ANALOG OSC (triangle, no drift) -> AMP ENVELOPE -> VOICE MIX -> OUTPUT. */
    Document steadyPatch (std::string* osc = nullptr)
    {
        std::string id;
        Document d = basicPatch (&id);
        edit::setParam (d, reg(), id, "wave", 1.0);   // TRI: no edges of its own, so a click shows
        edit::setParam (d, reg(), id, "drift", 0.0);
        if (osc != nullptr)
            *osc = id;
        return d;
    }
}

//==============================================================================
TEST_CASE ("ORGANIC SAMPLE: a recording that arrives while a key is down plays, and so does the next note", "[organic][live]")
{
    Document d;
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
    d.findNode (org)->strings["sample"] = "samples/bowed-c3.wav";
    Player p;
    auto loading = std::make_shared<SampleAsset>();
    loading->status = SampleAsset::Status::loading;
    p.options.assets = [loading] (const std::string&) { return loading; };
    p.publish (d);
    p.run (40, { Player::on (48) });
    CHECK (p.tail (2000) == 0.0);                 // nothing to play yet: silent, not broken
    CHECK (p.engine.activeVoices() == 1);
    // the loader publishes the playable recording: the owner recompiles (Session::tick)
    p.options.assets = factoryAssets();
    p.publish (d);
    p.run (80);
    CHECK (p.tail (2000) > 0.01);                 // the held key sounds in the new graph
    p.run (40, { Player::off (48) });
    p.run (80, { Player::on (55) });
    CHECK (p.tail (2000) > 0.01);
    CHECK (test::finite (p.left));
}

TEST_CASE ("ORGANIC SAMPLE: every note sounds with one voice, in mono and legato, and after a one-shot ended", "[organic][live]")
{
    Document d;
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
    d.findNode (org)->strings["sample"] = "samples/bowed-c3.wav";
    {
        Document one = d;
        one.settings.polyphony = 1;
        Player p;
        p.options.assets = factoryAssets();
        p.publish (one);
        for (int i = 0; i < 6; ++i)
        {
            p.run (30, { Player::on (48 + i) });
            INFO ("note " << i);
            CHECK (p.tail (2000) > 0.005);
            p.run (4, { Player::off (48 + i) });
        }
    }
    for (const int mode : { 1, 2 })
    {
        Document mono = d;
        mono.settings.voiceMode = mode;
        Player p;
        p.options.assets = factoryAssets();
        p.publish (mono);
        p.run (30, { Player::on (48) });
        p.run (30, { Player::on (55) });
        CHECK (p.tail (2000) > 0.005);
        p.run (30, { Player::off (55) });
        CHECK (p.tail (2000) > 0.005);
        p.run (200, { Player::off (48) });
        p.run (30, { Player::on (60) });
        CHECK (p.tail (2000) > 0.005);
    }
    {
        // LOOP off: the recording plays once; the next key plays it again
        Document once = d;
        edit::setParam (once, reg(), org, "loop", 0.0);
        Player p;
        p.options.assets = factoryAssets();
        p.publish (once);
        p.run (2000, { Player::on (48) });
        CHECK (p.tail (2000) == 0.0);
        p.run (30, { Player::on (52) });
        CHECK (p.tail (2000) > 0.005);
    }
}

TEST_CASE ("ORGANIC SAMPLE: START and REVERSE apply to the very first note", "[organic][live]")
{
    Document d;
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
    d.findNode (org)->strings["sample"] = "samples/bowed-c3.wav";
    auto first = [&] (double reverse, double start) {
        Document x = d;
        edit::setParam (x, reg(), org, "reverse", reverse);
        edit::setParam (x, reg(), org, "start", start);
        Player p;
        p.options.assets = factoryAssets();
        p.publish (x);
        p.run (30, { Player::on (48) });
        return p.left;
    };
    const auto forward = first (0.0, 0.0);
    CHECK (first (1.0, 0.0) != forward);
    CHECK (first (0.0, 0.5) != forward);
}

//==============================================================================
TEST_CASE ("KEYBOARD: an octave change under held keys neither moves nor hangs them; the next notes use it", "[event][transpose][lifecycle]")
{
    std::string osc;
    Document d = steadyPatch (&osc);
    const auto keys = nodeOfType (d, "event.keyboard");
    Player p;
    p.publish (d);
    p.run (60, { Player::on (57), Player::on (64) });
    const double before = test::fundamental (p.left, 48000.0, p.left.size() - 4800, 2400, 60.0, 1500.0);
    const int swaps = p.engine.swapsCompleted();
    REQUIRE (p.engine.latest()->setParam (keys, "octave", 2.0));   // (what Session::setParam does now: live, no rebuild)
    const std::size_t at = p.left.size();
    p.run (60);
    CHECK (p.engine.swapsCompleted() == swaps);
    CHECK (p.engine.activeVoices() == 2);
    // the held chord still sounds at the pitch it started, with no click at the change
    const double after = test::fundamental (p.left, 48000.0, p.left.size() - 4800, 2400, 60.0, 1500.0);
    CHECK (std::abs (1200.0 * std::log2 (after / before)) < 5.0);
    CHECK (maxStep (p.left, at - 256, at + 4096) < 1.5 * maxStep (p.left, at - 4096, at - 256));
    // its keys released: every voice ends (a note-off transposed to another pitch would hang)
    p.run (400, { Player::off (57), Player::off (64) });
    CHECK (p.engine.activeVoices() == 0);
    // the next note is two octaves up
    p.run (60, { Player::on (45) });
    const double next = test::fundamental (p.left, 48000.0, p.left.size() - 4800, 2400, 60.0, 2000.0);
    CHECK (std::abs (1200.0 * std::log2 (next / (110.0 * 4.0))) < 5.0);
}

TEST_CASE ("KEYBOARD: a key that started inside the range or the keyboard is released even when a change moves it outside", "[event][transpose][lifecycle]")
{
    Document d = steadyPatch();
    const auto keys = nodeOfType (d, "event.keyboard");
    Player p;
    p.publish (d);
    p.run (40, { Player::on (100) });
    REQUIRE (p.engine.activeVoices() == 1);
    p.engine.latest()->setParam (keys, "octave", 3.0);    // 100 + 36 is past the keyboard
    p.run (4);
    p.run (400, { Player::off (100) });
    CHECK (p.engine.activeVoices() == 0);
    // and one whose note-on never passed makes no voice and no stray release
    p.run (20, { Player::on (110) });
    CHECK (p.engine.activeVoices() == 0);
    p.engine.latest()->setParam (keys, "octave", 0.0);
    p.run (20, { Player::off (110) });
    CHECK (p.engine.activeVoices() == 0);
    // the RANGE narrowed under a held key
    p.run (40, { Player::on (60) });
    p.engine.latest()->setParam (keys, "high", 40.0);
    p.run (400, { Player::off (60) });
    CHECK (p.engine.activeVoices() == 0);
}

TEST_CASE ("KEYBOARD into an ARPEGGIATOR: an octave change under a held chord leaves no note running", "[event][transpose][lifecycle]")
{
    std::string osc;
    Document d = steadyPatch (&osc);
    REQUIRE (edit::addModule (d, reg(), "event.arp", -100, 0, osc).ok);
    const auto keys = nodeOfType (d, "event.keyboard");
    Player p;
    p.timing.valid = true;
    p.timing.playing = false;
    p.timing.bpm = 120.0;
    p.publish (d);
    p.run (400, { Player::on (48), Player::on (55), Player::on (60) });
    CHECK (p.tail (8000) > 0.005);
    p.engine.latest()->setParam (keys, "octave", 1.0);
    p.run (200);
    p.run (1200, { Player::off (48), Player::off (55), Player::off (60) });   // 3 s: every arp step and release long over
    CHECK (p.engine.activeVoices() == 0);
    CHECK (p.tail (8000) < 1.0e-5);
}

TEST_CASE ("KEY SPLIT and SCALE / TRANSPOSE release what they let through when their settings move", "[event][transpose][lifecycle]")
{
    for (const char* type : { "event.split", "event.scale" })
    {
        std::string osc;
        Document d = steadyPatch (&osc);
        const auto added = edit::addModule (d, reg(), type, -100, 0, osc);
        INFO (type << ": " << added.message);
        REQUIRE (added.ok);
        Player p;
        p.publish (d);
        p.run (40, { Player::on (72) });
        const int sounding = p.engine.activeVoices();
        auto* g = p.engine.latest();
        if (std::string (type) == "event.split")
        {
            g->setParam (added.focus, "key", sounding > 0 ? 100.0 : 20.0);
            g->setParam (added.focus, "side", sounding > 0 ? 0.0 : 1.0);
        }
        else
        {
            g->setParam (added.focus, "semitones", 7.0);
            g->setParam (added.focus, "octave", 2.0);
        }
        p.run (4);
        p.run (400, { Player::off (72) });
        CHECK (p.engine.activeVoices() == 0);
    }
}

//==============================================================================
TEST_CASE ("MUTE, SOLO and BYPASS are ramps in the running graph: no rebuild, no click, no restarted note", "[source][mute][solo]")
{
    std::string osc;
    Document d = steadyPatch (&osc);
    const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, osc).focus;
    REQUIRE_FALSE (filter.empty());
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int block : { 16, 64, 512 })
        {
            Player p (block, rate);
            p.publish (d);
            const int settle = static_cast<int> (0.4 * rate) / block;
            p.run (settle, { Player::on (57) });
            const int swaps = p.engine.swapsCompleted();
            const auto window = static_cast<std::size_t> (0.04 * rate);
            const double steady = maxStep (p.left, p.left.size() - window, p.left.size());
            REQUIRE (steady > 0.0);
            auto* g = p.engine.latest();
            const int some = std::max (1, static_cast<int> (0.07 * rate) / block);
            // at several phases of the wave: mute, unmute, bypass the filter, bring it back
            for (int round = 0; round < 5; ++round)
            {
                for (int action = 0; action < 4; ++action)
                {
                    if (action == 0) g->setGain (osc, 0.0f);
                    if (action == 1) g->setGain (osc, 1.0f);
                    if (action == 2) g->setEnabled (filter, false);
                    if (action == 3) g->setEnabled (filter, true);
                    const std::size_t at = p.left.size();
                    p.run (some + round);
                    INFO ("rate " << rate << " block " << block << " round " << round << " action " << action);
                    // (a hard switch would step by the wave's own height: tens of times this)
                    CHECK (maxStep (p.left, at, p.left.size()) < 3.0 * steady + 1.0e-4);
                    if (action == 0)
                        CHECK (p.tail (static_cast<std::size_t> (0.02 * rate)) < 1.0e-6);   // muted is silent
                    else
                        CHECK (p.tail (static_cast<std::size_t> (0.02 * rate)) > 0.01);
                }
            }
            CHECK (p.engine.swapsCompleted() == swaps);   // never a rebuilt graph
            CHECK (p.engine.activeVoices() == 1);         // the note never restarted or ended
            CHECK (test::finite (p.left));
        }
}

TEST_CASE ("SOLO isolates sources at their own outputs; MUTE wins over SOLO; groups carry both", "[source][mute][solo]")
{
    // Two oscillators an octave apart from one keyboard, into one voice mix.
    std::string a;
    Document d = steadyPatch (&a);
    const auto added = edit::addModule (d, reg(), "osc.analog", 0, 240, a);
    REQUIRE (added.ok);
    const auto b = added.focus;
    edit::setParam (d, reg(), b, "wave", 1.0);
    edit::setParam (d, reg(), b, "drift", 0.0);
    edit::setParam (d, reg(), b, "octave", 1.0);
    auto levels = [&] (const Document& doc) {
        OfflineRenderer r;
        const auto out = test::render (r, doc, { { 57, 0.0, 1.0 } }, 1.0);
        return std::pair<double, double> { test::toneLevel (out.left, 48000.0, 220.0, 24000, 9600), test::toneLevel (out.left, 48000.0, 440.0, 24000, 9600) };
    };
    const auto both = levels (d);
    REQUIRE (both.first > 0.001);
    REQUIRE (both.second > 0.001);

    Document soloA = d;
    soloA.findNode (a)->solo = true;
    CHECK (audibleGain (soloA, reg(), *soloA.findNode (a)) == 1.0);
    CHECK (audibleGain (soloA, reg(), *soloA.findNode (b)) == 0.0);
    CHECK (anySolo (soloA, reg()));
    // B's own tone is gone; A's stays as it was. (A's triangle has a little 440 Hz of its own? no:
    // a triangle has only odd harmonics, so 440 Hz is B alone.)
    CHECK (levels (soloA).first == Catch::Approx (both.first).epsilon (0.05));
    CHECK (levels (soloA).second < 0.02 * both.second);

    Document bothSolo = soloA;
    bothSolo.findNode (b)->solo = true;
    CHECK (levels (bothSolo).second == Catch::Approx (both.second).epsilon (0.05));

    Document mutedSolo = bothSolo;
    mutedSolo.findNode (a)->muted = true;   // muted and soloed: mute wins
    CHECK (audibleGain (mutedSolo, reg(), *mutedSolo.findNode (a)) == 0.0);
    CHECK (levels (mutedSolo).first < 0.02 * both.first);
    CHECK (levels (mutedSolo).second == Catch::Approx (both.second).epsilon (0.05));

    // what is not a source is never silenced by a solo (the envelope, the mix)
    for (const auto& n : soloA.nodes)
        if (reg().find (n.type)->role != NodeRole::source)
            CHECK (audibleGain (soloA, reg(), n) == 1.0);

    // a group's S and M reach the sources inside it, and leave their own flags alone
    Document grouped = d;
    const auto made = edit::group (grouped, reg(), { b }, "UPPER");
    REQUIRE (made.ok);
    grouped.findGroup (made.focus)->solo = true;
    CHECK (audibleGain (grouped, reg(), *grouped.findNode (b)) == 1.0);
    CHECK (audibleGain (grouped, reg(), *grouped.findNode (a)) == 0.0);
    CHECK_FALSE (grouped.findNode (b)->solo);
    grouped.findGroup (made.focus)->muted = true;
    CHECK (audibleGain (grouped, reg(), *grouped.findNode (b)) == 0.0);

    // saved and read back; a patch without the keys has neither (schema 1 files as before)
    Document back;
    REQUIRE (loadFromString (saveToString (mutedSolo), back).ok);
    CHECK (back.findNode (a)->muted);
    CHECK (back.findNode (a)->solo);
    CHECK (saveToString (d).find ("\"solo\"") == std::string::npos);
    CHECK (saveToString (d).find ("\"muted\"") == std::string::npos);
}

TEST_CASE ("ORGANIC SAMPLE: a recording with no clear pitch is played as recorded on middle C, not octaves below hearing", "[organic][live]")
{
    // The reported silence, reproduced: speech-like material. OSP's analysis has no confidence in
    // a pitch (well under 0.4) but still reports a root (far above the keys here), and the note
    // played 3 to 4 octaves down: below 20 Hz, a "ready" source that cannot be heard.
    const double rate = 48000.0;
    const int frames = 48000 * 6;
    std::vector<float> speech (static_cast<std::size_t> (frames));
    for (int i = 0; i < frames; ++i)
        speech[static_cast<std::size_t> (i)] = static_cast<float> (0.3 * std::sin (i * 0.01 * (1.0 + 0.5 * std::sin (i * 0.0003))) * (0.5 + 0.5 * std::sin (i * 0.0007)));
    const std::string path = std::string (MODULAR_SOURCE_DIR) + "/tests/no-pitch-probe.wav";
    std::string error;
    REQUIRE (wav::write (path, speech.data(), speech.data(), frames, rate, error));
    const auto asset = library().loadNow (path);
    std::remove (path.c_str());
    REQUIRE (asset != nullptr);
    REQUIRE (asset->status == SampleAsset::Status::ready);
    INFO ("root " << asset->rootMidi << " confidence " << asset->pitchConfidence);
    CHECK_FALSE (asset->pitched());
    CHECK (std::abs (asset->rootMidi - 60.0) > 24.0);   // (the guess that made it inaudible)

    Document d;
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
    d.findNode (org)->strings["sample"] = "x";
    OfflineRenderer r;
    const auto out = test::render (r, d, { { 60, 0.0, 2.0 } }, 2.0, 256, rate, [asset] (const std::string&) { return asset; });
    auto crossings = [] (const std::vector<float>& x, std::size_t from, std::size_t to) {
        int n = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            n += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
        return n;
    };
    // as recorded: about as many zero crossings as the recording itself over the same second
    const int played = crossings (out.left, 24000, 72000), recorded = crossings (speech, 24000, 72000);
    INFO ("crossings played " << played << " recorded " << recorded);
    CHECK (played > recorded / 2);
    CHECK (played < recorded * 2);
    CHECK (test::rms (out.left, 24000, 72000) > 0.01);
    // a tonal recording keeps its analysed root (the factory bowed C3)
    CHECK (library().loadNow (test::presetPath ("factory/samples/bowed-c3.wav"))->pitched());
}

//==============================================================================
TEST_CASE ("Several sources of one note stream into ONE per-voice filter: it hears their sum, and is not copied", "[routing][fanin]")
{
    // KEYBOARD -> OSC A + OSC B (an octave up) -> [sum per voice] -> one FILTER -> AMP ENV -> VOICE MIX
    std::string a;
    Document d = steadyPatch (&a);
    const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, a).focus;
    REQUIRE_FALSE (filter.empty());
    edit::setParam (d, reg(), filter, "cutoff", 18000.0);
    auto* second = d.addNode (reg(), "osc.analog", 0, 240);
    const auto b = second->id;
    second->domain = Domain::voice;
    edit::setParam (d, reg(), b, "wave", 1.0);
    edit::setParam (d, reg(), b, "drift", 0.0);
    edit::setParam (d, reg(), b, "octave", 1.0);
    REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", b, "notes").ok);
    const std::size_t nodesBefore = d.nodes.size();
    const auto joined = edit::connectSmart (d, reg(), b, "out", filter, "in");
    INFO (joined.message);
    REQUIRE (joined.ok);
    CHECK (joined.message.find ("2 sources") != std::string::npos);
    CHECK (d.nodes.size() == nodesBefore);                       // no mixer, no second filter
    CHECK (d.findNode (filter)->domain == Domain::voice);
    CHECK_FALSE (Validator (reg()).validate (d).hasErrors());

    OfflineRenderer r;
    const auto out = test::render (r, d, { { 57, 0.0, 1.0 } }, 1.0);
    const double low = test::toneLevel (out.left, 48000.0, 220.0, 24000, 9600), high = test::toneLevel (out.left, 48000.0, 440.0, 24000, 9600);
    CHECK (low > 0.001);
    CHECK (high > 0.001);
    // one voice chain (one pool), one filter instance per voice
    REQUIRE (out.graph != nullptr);
    CHECK (out.graph->lanes.size() == 1);
    const auto& fn = out.graph->nodes[static_cast<std::size_t> (out.graph->nodeIndexById.at (filter))];
    CHECK (static_cast<int> (fn.instances.size()) == d.settings.polyphony);
    // closing the filter takes BOTH down: it really hears the sum
    Document closed = d;
    edit::setParam (closed, reg(), filter, "cutoff", 60.0);
    OfflineRenderer r2;
    const auto dark = test::render (r2, closed, { { 57, 0.0, 1.0 } }, 1.0);
    CHECK (test::toneLevel (dark.left, 48000.0, 440.0, 24000, 9600) < 0.2 * high);
    CHECK (test::toneLevel (dark.left, 48000.0, 220.0, 24000, 9600) < 0.7 * low);
    // a third source the same way
    auto* third = d.addNode (reg(), "src.noise", 0, 480);
    const auto c = third->id;
    third->domain = Domain::voice;
    REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", c, "notes").ok);
    const auto three = edit::connectSmart (d, reg(), c, "out", filter, "in");
    CHECK (three.ok);
    CHECK (three.message.find ("3 sources") != std::string::npos);
    // saved and read back unchanged
    Document back;
    REQUIRE (loadFromString (saveToString (d), back).ok);
    CHECK (saveToString (back) == saveToString (d));
}

TEST_CASE ("Two chains with their own ARPs into one filter: it runs once, globally, and each chain keeps its notes", "[routing][fanin][routing-domains]")
{
    // Chain A: KEYBOARD -> ARP -> OSC A -> FILTER -> AMP ENV -> VOICE MIX -> OUTPUT
    std::string a;
    Document d = steadyPatch (&a);
    const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, a).focus;
    edit::setParam (d, reg(), filter, "cutoff", 18000.0);
    const auto arpA = edit::addModule (d, reg(), "event.arp", -100, 0, a).focus;
    REQUIRE_FALSE (arpA.empty());
    // Chain B: KEYBOARD -> ARP (another rate) -> OSC B, not yet routed anywhere
    auto* second = d.addNode (reg(), "osc.analog", 0, 300);
    const auto b = second->id;
    second->domain = Domain::voice;
    edit::setParam (d, reg(), b, "wave", 1.0);
    edit::setParam (d, reg(), b, "drift", 0.0);
    edit::setParam (d, reg(), b, "octave", 2.0);
    auto* arp2 = d.addNode (reg(), "event.arp", -200, 300);
    const auto arpB = arp2->id;
    edit::setParam (d, reg(), arpB, "rate", 2.0);
    REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", arpB, "notes").ok);
    REQUIRE (edit::connect (d, reg(), arpB, "notes", b, "notes").ok);

    const Document before = d;
    const auto shared = edit::connectSmart (d, reg(), b, "out", filter, "in");
    INFO (shared.message);
    REQUIRE (shared.ok);
    CHECK (shared.message.find ("runs once") != std::string::npos);
    CHECK (d.findNode (filter)->domain == Domain::global);
    const auto report = Validator (reg()).validate (d);
    for (const auto& i : report.issues)
        INFO (i.message);
    CHECK_FALSE (report.hasErrors());

    OfflineRenderer r;
    RenderSettings s;
    s.seconds = 3.0;
    s.notes = { { 45, 0.0, 3.0 }, { 52, 0.0, 3.0 } };
    const auto out = r.render (d, s);
    REQUIRE (out.graph != nullptr);
    CHECK (out.graph->lanes.size() == 2);   // two voice pools: the notes of one never play the other
    const auto& fn = out.graph->nodes[static_cast<std::size_t> (out.graph->nodeIndexById.at (filter))];
    CHECK (fn.instances.size() == 1);       // the filter runs once
    CHECK (test::finite (out.left));
    CHECK (test::rms (out.left, 48000, 144000) > 0.005);
    // each chain alone is heard through it (solo by mute): both feed the one filter
    for (const auto& muted : { a, b })
    {
        Document one = d;
        one.findNode (muted)->muted = true;
        OfflineRenderer rr;
        const auto solo = rr.render (one, s);
        INFO ("with " << muted << " muted");
        CHECK (test::rms (solo.left, 48000, 144000) > 0.002);
        CHECK (test::rms (solo.left, 48000, 144000) < test::rms (out.left, 48000, 144000));
    }
    // released: nothing hangs
    Player p;
    p.timing.valid = true;
    p.timing.bpm = 120.0;
    p.publish (d);
    p.run (400, { Player::on (45), Player::on (52) });
    p.run (1500, { Player::off (45), Player::off (52) });
    CHECK (p.engine.activeVoices() == 0);
    CHECK (p.tail (8000) < 1.0e-5);

    // a per-voice-only processor cannot be shared that way: refused with the reason, nothing changed
    Document m = before;
    const auto morph = m.addNode (reg(), "proc.morph", 300, 300)->id;
    m.findNode (morph)->domain = Domain::voice;
    REQUIRE (edit::connect (m, reg(), a, "out", morph, "a").ok);
    const auto text = saveToString (m);
    const auto refused = edit::connectSmart (m, reg(), b, "out", morph, "b");
    INFO (refused.message);
    if (! refused.ok)
        CHECK (saveToString (m) == text);
}

TEST_CASE ("Voice audio dragged into a global effect goes through its own VOICE MIX; what cannot work is still refused", "[routing][fanin][routing-domains]")
{
    std::string a;
    Document d = steadyPatch (&a);
    const auto space = edit::addModule (d, reg(), "fx.space", 600, 0, nodeOfType (d, "util.voicemix")).focus;
    REQUIRE_FALSE (space.empty());
    REQUIRE (d.findNode (space)->domain == Domain::global);
    auto* second = d.addNode (reg(), "osc.analog", 0, 300);
    const auto b = second->id;
    second->domain = Domain::voice;
    REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", b, "notes").ok);
    const std::size_t mixes = static_cast<std::size_t> (std::count_if (d.nodes.begin(), d.nodes.end(), [] (const NodeModel& n) { return n.type == "util.voicemix"; }));
    const auto joined = edit::connectSmart (d, reg(), b, "out", space, "in");
    INFO (joined.message);
    REQUIRE (joined.ok);
    CHECK (static_cast<std::size_t> (std::count_if (d.nodes.begin(), d.nodes.end(), [] (const NodeModel& n) { return n.type == "util.voicemix"; })) == mixes + 1);
    CHECK_FALSE (Validator (reg()).validate (d).hasErrors());
    OfflineRenderer r;
    const auto out = test::render (r, d, { { 57, 0.0, 1.0 } }, 1.5);
    CHECK (test::rms (out.left, 24000, 60000) > 0.005);
    const auto& sn = out.graph->nodes[static_cast<std::size_t> (out.graph->nodeIndexById.at (space))];
    CHECK (sn.instances.size() == 1);

    // still refused, with the reason: notes into audio, audio into itself (a loop)
    const auto text = saveToString (d);
    CHECK_FALSE (edit::connectSmart (d, reg(), nodeOfType (d, "event.keyboard"), "notes", space, "in").ok);
    CHECK_FALSE (edit::connectSmart (d, reg(), space, "out", space, "in").ok);
    CHECK (saveToString (d) == text);
}

//==============================================================================
namespace
{
    /** True when no two tiles of the open view are closer than the clearance. */
    bool tidy (const Document& d, float cx = 32.0f, float cy = 24.0f, std::string* which = nullptr)
    {
        std::vector<std::pair<std::string, edit::TileRect>> tiles;
        for (const auto& g : d.groups)
            if (g.parent == d.view.currentGroup)
                tiles.emplace_back (g.id, edit::tileRect (d, reg(), g.id));
        for (const auto& n : d.nodes)
            if (n.group == d.view.currentGroup)
                tiles.emplace_back (n.id, edit::tileRect (d, reg(), n.id));
        for (std::size_t i = 0; i < tiles.size(); ++i)
            for (std::size_t j = i + 1; j < tiles.size(); ++j)
            {
                const auto& a = tiles[i].second;
                const auto& b = tiles[j].second;
                if (! std::isfinite (a.x) || ! std::isfinite (a.y))
                    return false;
                if (a.x < b.x + b.w + cx && b.x < a.x + a.w + cx && a.y < b.y + b.h + cy && b.y < a.y + a.h + cy)
                {
                    if (which != nullptr)
                        *which = tiles[i].first + " and " + tiles[j].first;
                    return false;
                }
            }
        return true;
    }
}

TEST_CASE ("Layout: no tile ever overlaps another; what was just placed stays put and the others make room", "[ui][layout]")
{
    // every factory patch is tidy as it ships, and a pass changes nothing
    for (const auto& p : factory::patches (reg()))
    {
        Document d = p;
        INFO (d.name);
        std::string which;
        CHECK (tidy (d, 32.0f, 24.0f, &which));
        INFO (which);
        CHECK (edit::resolveOverlaps (d, reg()).empty());
    }

    // a module dropped onto another: it stays where it was dropped, the other moves, by little
    {
        std::string osc;
        Document d = steadyPatch (&osc);
        const auto env = nodeOfType (d, "proc.vca");
        auto* a = d.findNode (osc);
        const float ex = d.findNode (env)->x, ey = d.findNode (env)->y;
        a->x = ex + 16.0f;
        a->y = ey + 8.0f;
        const float ax = a->x, ay = a->y;
        const auto wires = d.connections.size();
        const auto moved = edit::resolveOverlaps (d, reg(), { osc });
        CHECK (tidy (d));
        CHECK (d.findNode (osc)->x == ax);
        CHECK (d.findNode (osc)->y == ay);
        CHECK (std::find (moved.begin(), moved.end(), env) != moved.end());
        CHECK (std::abs (d.findNode (env)->x - ex) + std::abs (d.findNode (env)->y - ey) < 400.0f);
        CHECK (d.connections.size() == wires);
        CHECK (edit::resolveOverlaps (d, reg(), { osc }).empty());   // settled: nothing jitters
    }

    // inserted on a cable, added after the selection, six sources stacked on one spot: always tidy
    {
        std::string osc;
        Document d = steadyPatch (&osc);
        for (const char* type : { "proc.filter", "proc.drive", "fx.echo", "fx.space", "proc.tone" })
        {
            const auto made = edit::addModule (d, reg(), type, d.findNode (osc)->x + 40.0f, d.findNode (osc)->y + 10.0f, osc);
            REQUIRE (made.ok);
            edit::resolveOverlaps (d, reg(), { made.focus });
            std::string which;
            const bool ok = tidy (d, 32.0f, 24.0f, &which);
            INFO (type << " " << which);
            CHECK (ok);
        }
        for (int i = 0; i < 5; ++i)
        {
            const auto made = edit::addModule (d, reg(), "osc.analog", 0.0f, 0.0f);   // all on one spot
            if (! made.ok)
                break;   // (the six-source budget)
            edit::resolveOverlaps (d, reg(), { made.focus });
            CHECK (tidy (d));
        }
        // through a save: the same places
        Document back;
        REQUIRE (loadFromString (saveToString (d), back).ok);
        CHECK (saveToString (back) == saveToString (d));
        CHECK (edit::resolveOverlaps (back, reg()).empty());
    }
}

TEST_CASE ("Layout: any pile of modules settles, deterministically, in one pass (property)", "[ui][layout]")
{
    const char* const types[] = { "osc.analog", "proc.filter", "proc.vca", "fx.echo", "fx.space", "ctl.lfo", "event.arp", "util.mixer", "proc.morph", "src.noise" };
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        osp::Prng r (seed);
        Document d;
        const int count = 3 + static_cast<int> (r.nextBelow (28));
        std::vector<std::string> ids;
        for (int i = 0; i < count; ++i)
        {
            // crowded on purpose: a few hundred px for up to thirty tiles
            auto* n = d.addNode (reg(), types[r.nextBelow (10)], static_cast<float> (r.uniform (-300.0, 300.0)), static_cast<float> (r.uniform (-200.0, 200.0)));
            REQUIRE (n != nullptr);
            ids.push_back (n->id);
        }
        std::vector<std::string> anchors;
        if (seed % 2 == 0)
            anchors.push_back (ids[r.nextBelow (static_cast<std::uint64_t> (count))]);
        const auto anchorAt = anchors.empty() ? std::pair<float, float> { 0.0f, 0.0f } : std::pair<float, float> { d.findNode (anchors[0])->x, d.findNode (anchors[0])->y };
        Document twin = d;
        edit::resolveOverlaps (d, reg(), anchors);
        edit::resolveOverlaps (twin, reg(), anchors);
        std::string which;
        INFO ("seed " << seed << " count " << count);
        const bool ok = tidy (d, 32.0f, 24.0f, &which);
        INFO (which);
        CHECK (ok);
        CHECK (saveToString (d) == saveToString (twin));                 // the same pile, the same result
        CHECK (edit::resolveOverlaps (d, reg(), anchors).empty());      // and it stays
        CHECK (d.nodes.size() == static_cast<std::size_t> (count));
        if (! anchors.empty())
        {
            CHECK (d.findNode (anchors[0])->x == anchorAt.first);
            CHECK (d.findNode (anchors[0])->y == anchorAt.second);
        }
        for (const auto& n : d.nodes)
        {
            CHECK (std::isfinite (n.x));
            CHECK (std::isfinite (n.y));
        }
    }
}

TEST_CASE ("Layout: the worst pile (32 modules on one spot) settles well inside one interface frame", "[ui][layout][perf]")
{
    const char* const types[] = { "osc.analog", "proc.filter", "proc.vca", "fx.echo", "fx.space", "ctl.lfo", "event.arp", "util.mixer" };
    double worstMs = 0.0;
    for (int run = 0; run < 5; ++run)
    {
        Document d;
        for (int i = 0; i < 32; ++i)
            REQUIRE (d.addNode (reg(), types[i % 8], 0.0f, 0.0f) != nullptr);
        const auto started = std::chrono::steady_clock::now();
        edit::resolveOverlaps (d, reg());
        worstMs = std::max (worstMs, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count());
        CHECK (tidy (d));
    }
    INFO ("32 modules stacked on one spot, laid out in " << worstMs << " ms (worst of 5)");
    CHECK (worstMs < 8.0);   // (a 60 Hz frame is 16.7 ms; this runs on the message thread only)
    std::printf ("layout: 32 stacked modules settle in %.3f ms (worst of 5)\n", worstMs);
}

//==============================================================================
// Listening demos of this pass (hidden: run with "[.demos]" and MODULAR_DEMO_DIR set; see
// tools/render-demos.sh). Deterministic: the same build writes the same files.
TEST_CASE ("Listening demos: shared filters, an unpitched recording, octave and mute / solo under held notes", "[.demos]")
{
    const char* dir = std::getenv ("MODULAR_DEMO_DIR");
    if (dir == nullptr || *dir == 0)
    {
        WARN ("set MODULAR_DEMO_DIR to a folder to write the demos");
        return;
    }
    const std::string out = dir;
    auto save = [&] (const std::string& name, const std::vector<float>& left, const std::vector<float>& right, double rate = 48000.0) {
        std::string error;
        REQUIRE (wav::write (out + "/" + name + ".wav", left.data(), right.data(), static_cast<long long> (left.size()), rate, error));
        CHECK (test::rms (left, 0, left.size()) > 0.001);
    };
    auto secondOsc = [] (Document& d, int octave, float y) {
        auto* n = d.addNode (reg(), "osc.analog", 0, y);
        n->domain = Domain::voice;
        edit::setParam (d, reg(), n->id, "octave", static_cast<double> (octave));
        return n->id;
    };

    // 1. Two oscillators of the same notes into ONE per-voice filter (summed per voice), the
    //    filter closing over the phrase: both are darkened together.
    {
        std::string a;
        Document d = basicPatch (&a);
        const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, a).focus;
        const auto b = secondOsc (d, 1, 240);
        REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", b, "notes").ok);
        REQUIRE (edit::connectSmart (d, reg(), b, "out", filter, "in").ok);
        edit::setParam (d, reg(), filter, "cutoff", 900.0);
        edit::setParam (d, reg(), filter, "resonance", 0.45);
        OfflineRenderer r;
        const auto audio = test::render (r, d, { { 45, 0.0, 1.4 }, { 52, 0.5, 1.4 }, { 57, 1.0, 1.4 }, { 60, 1.5, 2.0 } }, 4.5);
        save ("1-two-sources-one-voice-filter", audio.left, audio.right);
    }
    // 2. Two chains, each with its own ARP, into one filter: it runs once, globally (each
    //    chain through its own VOICE MIX), and each arpeggio keeps its own notes.
    {
        std::string a;
        Document d = basicPatch (&a);
        const auto filter = edit::addModule (d, reg(), "proc.filter", 200, 0, a).focus;
        REQUIRE_FALSE (edit::addModule (d, reg(), "event.arp", -100, 0, a).focus.empty());
        const auto b = secondOsc (d, 2, 300);
        auto* arp = d.addNode (reg(), "event.arp", -200, 300);
        const auto arpB = arp->id;
        edit::setParam (d, reg(), arpB, "rate", 2.0);
        REQUIRE (edit::connect (d, reg(), nodeOfType (d, "event.keyboard"), "notes", arpB, "notes").ok);
        REQUIRE (edit::connect (d, reg(), arpB, "notes", b, "notes").ok);
        REQUIRE (edit::connectSmart (d, reg(), b, "out", filter, "in").ok);
        edit::setParam (d, reg(), filter, "cutoff", 1400.0);
        edit::setParam (d, reg(), filter, "resonance", 0.5);
        OfflineRenderer r;
        RenderSettings s;
        s.seconds = 6.0;
        s.notes = { { 45, 0.0, 5.0 }, { 52, 0.0, 5.0 }, { 57, 0.0, 5.0 } };
        const auto audio = r.render (d, s);
        save ("2-two-arps-one-global-filter", audio.left, audio.right);
    }
    // 3. A recording with no clear pitch on ORGANIC SAMPLE: C4 plays it as recorded, C5 an
    //    octave up (before: three to four octaves down, below hearing).
    {
        const double rate = 48000.0;
        const int frames = 48000 * 6;
        std::vector<float> speech (static_cast<std::size_t> (frames));
        for (int i = 0; i < frames; ++i)
            speech[static_cast<std::size_t> (i)] = static_cast<float> (0.3 * std::sin (i * 0.01 * (1.0 + 0.5 * std::sin (i * 0.0003))) * (0.5 + 0.5 * std::sin (i * 0.0007)));
        const std::string path = out + "/3-unpitched-recording-source.wav";
        std::string error;
        REQUIRE (wav::write (path, speech.data(), speech.data(), frames, rate, error));
        const auto asset = library().loadNow (path);
        REQUIRE (asset != nullptr);
        Document d;
        const auto org = edit::addModule (d, reg(), "src.organic", 0, 0).focus;
        d.findNode (org)->strings["sample"] = "x";
        OfflineRenderer r;
        const auto audio = test::render (r, d, { { 60, 0.0, 2.0 }, { 72, 2.2, 1.8 } }, 4.5, 256, rate, [asset] (const std::string&) { return asset; });
        save ("3-unpitched-recording-played-c4-then-c5", audio.left, audio.right);
    }
    // 4. KEYBOARD OCTAVE moved twice under a held chord: the chord stays where it is and ends
    //    when released; the next chords are an octave up, then two.
    {
        std::string osc;
        Document d = basicPatch (&osc);
        const auto keys = nodeOfType (d, "event.keyboard");
        Player p;
        p.publish (d);
        const int second = 48000 / p.block;
        p.run (second, { Player::on (45), Player::on (52), Player::on (57) });
        REQUIRE (p.engine.latest()->setParam (keys, "octave", 1.0));
        p.run (second / 2);
        p.run (second / 2, { Player::off (45), Player::off (52), Player::off (57) });
        p.run (second, { Player::on (45), Player::on (52), Player::on (57) });
        REQUIRE (p.engine.latest()->setParam (keys, "octave", 2.0));
        p.run (second / 2, { Player::off (45), Player::off (52), Player::off (57) });
        p.run (second, { Player::on (45), Player::on (52), Player::on (57) });
        p.run (second, { Player::off (45), Player::off (52), Player::off (57) });
        CHECK (p.engine.activeVoices() == 0);
        save ("4-octave-moved-under-a-held-chord", p.left, p.right);
    }
    // 5. Two sources under one held note: A muted and back, B soloed and back, live (ramps in
    //    the running graph: no rebuild, no click, the note never restarts).
    {
        std::string a;
        Document d = basicPatch (&a);
        const auto b = edit::addModule (d, reg(), "osc.analog", 0, 240, a).focus;
        edit::setParam (d, reg(), b, "octave", 1.0);
        Player p;
        p.publish (d);
        const int half = 24000 / p.block;
        p.run (2 * half, { Player::on (45) });
        const int swaps = p.engine.swapsCompleted();
        auto* g = p.engine.latest();
        REQUIRE (g->setGain (a, 0.0f));   // M on A
        p.run (half);
        REQUIRE (g->setGain (a, 1.0f));
        p.run (half);
        REQUIRE (g->setGain (a, 0.0f));   // S on B (A is silenced at its own output)
        p.run (half);
        REQUIRE (g->setGain (a, 1.0f));
        REQUIRE (g->setGain (b, 0.0f));   // S on A
        p.run (half);
        REQUIRE (g->setGain (b, 1.0f));
        p.run (half);
        p.run (2 * half, { Player::off (45) });
        CHECK (p.engine.swapsCompleted() == swaps);
        save ("5-mute-and-solo-under-a-held-note", p.left, p.right);
    }
}

