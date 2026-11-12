// Reliability: what a musician does while notes sound (a recording arriving, an octave
// change, a mute, a solo, a shared filter) must neither go silent, nor click, nor hang a note.

#include "TestUtil.h"

#include "core/Prng.h"
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
