#include "TestUtil.h"

#include <algorithm>

using namespace andor::modular;
using test::reg;

TEST_CASE ("Quick Add: the first source plays; the second joins; a global source gets a MIXER", "[unit][edit]")
{
    Document d;
    auto r = edit::addModule (d, reg(), "osc.analog", 0, 0);
    REQUIRE (r.ok);
    CHECK (r.created.size() == 5);   // osc, keyboard, amp env, voice mix, output
    CHECK (r.message.find ("plays") != std::string::npos);
    r = edit::addModule (d, reg(), "src.noise", 0, 200);
    REQUIRE (r.ok);
    CHECK (r.message.find ("VOICE MIX") != std::string::npos);
    CHECK (test::nodeOfType (d, "util.voicemix", 1).empty());   // one VOICE MIX, summed
    r = edit::addModule (d, reg(), "src.rhythm", 0, 400);
    REQUIRE (r.ok);
    CHECK (r.message.find ("MIXER") != std::string::npos);
    Validator v (reg());
    CHECK_FALSE (v.validate (d).hasErrors());
}

TEST_CASE ("Insert on a cable, after a selection, and from a port", "[unit][edit]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    const auto before = d.connections.size();
    const auto r = edit::addModule (d, reg(), "proc.filter", 100, 0, osc);
    REQUIRE (r.ok);
    CHECK (r.message.find ("inserted FILTER") != std::string::npos);
    CHECK (d.connections.size() == before + 1);
    CHECK (d.findNode (r.focus)->domain == Domain::voice);   // on a voice cable: a voice filter
    // A global processor after VOICE MIX goes global.
    const auto mix = test::nodeOfType (d, "util.voicemix");
    const auto drive = edit::addModule (d, reg(), "proc.drive", 600, 0, mix);
    REQUIRE (drive.ok);
    CHECK (d.findNode (drive.focus)->domain == Domain::global);
    // LFO onto a selection: modulates its first musical parameter.
    const auto lfo = edit::addModule (d, reg(), "ctl.lfo", 0, 300, r.focus);
    REQUIRE (lfo.ok);
    CHECK (lfo.message.find ("modulates") != std::string::npos);
    // From a port: compatible types only.
    const auto types = edit::compatibleTypes (d, reg(), osc, "fm", false);
    REQUIRE_FALSE (types.empty());
    auto offered = [&] (const char* type) {
        return std::any_of (types.begin(), types.end(), [&] (const NodeDesc* t) { return t->type == type; });
    };
    CHECK (offered ("osc.analog"));        // audio-rate FM from another oscillator
    CHECK (offered ("ctl.lfo"));           // or a control
    CHECK_FALSE (offered ("fx.space"));    // global stereo audio cannot enter a voice
    CHECK_FALSE (offered ("event.arp"));   // notes are not FM
    Validator v (reg());
    CHECK_FALSE (v.validate (d).hasErrors());
}

TEST_CASE ("Undo and redo restore every smart action in one step", "[unit][edit]")
{
    edit::History h;
    Document d;
    h.push (d, "add osc");
    REQUIRE (edit::addModule (d, reg(), "osc.analog", 0, 0).ok);
    const auto afterOsc = saveToString (d);
    h.push (d, "add organic");
    const auto org = edit::addModule (d, reg(), "src.organic", 0, 200).focus;
    h.push (d, "morph");
    REQUIRE (edit::dropOnSource (d, reg(), org, test::nodeOfType (d, "osc.analog"), edit::DropAction::morph).ok);
    const auto afterMorph = saveToString (d);
    REQUIRE (h.undo (d));
    REQUIRE (h.undo (d));
    CHECK (saveToString (d) == afterOsc);
    REQUIRE (h.redo (d));
    REQUIRE (h.redo (d));
    CHECK (saveToString (d) == afterMorph);
    CHECK (h.undoLabel() == "morph");
}

TEST_CASE ("Drop a source on a source: LAYER, MORPH, MODULATE, REPLACE", "[unit][edit]")
{
    auto make = [] {
        Document d;
        edit::addModule (d, reg(), "osc.analog", 0, 0);
        edit::addModule (d, reg(), "osc.analog", 0, 200);
        return d;
    };
    {
        Document d = make();
        const auto a = test::nodeOfType (d, "osc.analog"), b = test::nodeOfType (d, "osc.analog", 1);
        const auto r = edit::dropOnSource (d, reg(), b, a, edit::DropAction::morph);
        REQUIRE (r.ok);
        CHECK (test::nodeOfType (d, "proc.vca", 1).empty());   // B's own AMP ENV went with its cable
        OfflineRenderer ren;
        CHECK (test::rms (test::render (ren, d, { { 57, 0.0, 0.5 } }, 0.6).left) > 0.01);
    }
    {
        Document d = make();
        const auto a = test::nodeOfType (d, "osc.analog"), b = test::nodeOfType (d, "osc.analog", 1);
        const auto r = edit::dropOnSource (d, reg(), b, a, edit::DropAction::modulate);
        REQUIRE (r.ok);
        CHECK (d.findNode (a)->params["fm"] > 0.0);
    }
    {
        Document d = make();
        const auto a = test::nodeOfType (d, "osc.analog"), b = test::nodeOfType (d, "osc.analog", 1);
        CHECK (edit::dropOnSource (d, reg(), b, a, edit::DropAction::layer).ok);
        CHECK (edit::dropOnSource (d, reg(), b, a, edit::DropAction::replace).ok);
        CHECK (d.findNode (a) == nullptr);
        Validator v (reg());
        CHECK_FALSE (v.validate (d).hasErrors());
    }
}

TEST_CASE ("Groups: save as a module, load it into another patch, its controls play", "[integration][edit][groups]")
{
    Document a;
    const auto osc = edit::addModule (a, reg(), "osc.analog", 0, 0).focus;
    const auto filter = edit::addModule (a, reg(), "proc.filter", 200, 0, osc).focus;
    const auto vca = test::nodeOfType (a, "proc.vca");
    const auto g = edit::group (a, reg(), { osc, filter, vca }, "TEST VOICE");
    REQUIRE (g.ok);
    REQUIRE (edit::exposeControl (a, reg(), g.focus, filter, "cutoff", "TONE", 0.0, 1.0));
    const auto text = edit::exportModule (a, g.focus);
    REQUIRE_FALSE (text.empty());

    Document b = test::basicPatch();
    const auto imported = edit::importModule (b, reg(), text, 400, 400);
    REQUIRE (imported.ok);
    CHECK (b.findGroup (imported.focus) != nullptr);
    CHECK (edit::groupMembers (b, imported.focus).size() == 3);
    Validator v (reg());
    CHECK (v.validate (b).sources == 2);   // flattened: the group's oscillator counts

    // The imported chain needs a route out: its AMP ENV into the patch's VOICE MIX.
    const auto innerVca = test::nodeOfType (b, "proc.vca", 1);
    REQUIRE (edit::connect (b, reg(), innerVca, "out", test::nodeOfType (b, "util.voicemix"), "in").ok);
    // ...and notes from the keyboard (cables across a module's edge are the patch's own).
    REQUIRE (edit::connect (b, reg(), test::nodeOfType (b, "event.keyboard"), "notes", test::nodeOfType (b, "osc.analog", 1), "notes").ok);
    b.findNode (test::nodeOfType (b, "osc.analog"))->enabled = false;   // only the module sounds
    // A macro drives the group control, which drives the inner cutoff.
    REQUIRE (edit::mapMacro (b, reg(), 0, imported.focus, "control.0", 0.0, 1.0));
    const auto innerFilter = test::nodeOfType (b, "proc.filter");
    OfflineRenderer r1, r2;
    RenderSettings s;
    s.seconds = 0.5;
    s.notes = { { 45, 0.0, 0.5 } };
    s.setMacros = true;
    s.macros[0] = 0.0f;
    const auto dark = r1.render (b, s);
    s.macros[0] = 1.0f;
    const auto bright = r2.render (b, s);
    CHECK (bright.graph->effectiveParam (innerFilter, "cutoff") > 10000.0);
    CHECK (dark.graph->effectiveParam (innerFilter, "cutoff") < 100.0);
    CHECK (test::toneLevel (bright.left, 48000.0, 3520.0, 9600, 9600) > 3.0 * test::toneLevel (dark.left, 48000.0, 3520.0, 9600, 9600));

    // A module that would break the budget is refused.
    Document full;
    for (int i = 0; i < 6; ++i)
        edit::addModule (full, reg(), "osc.analog", 0, 150.0f * static_cast<float> (i));
    CHECK_FALSE (edit::importModule (full, reg(), text, 0, 0).ok);
}

TEST_CASE ("EVOLVE: deterministic, valid, protective, one undo", "[unit][edit][evolve]")
{
    Document d;
    REQUIRE (loadFromFile (test::presetPath ("factory/scene-performance.andormodular"), d).ok);
    const auto original = saveToString (d);
    for (const auto intensity : { edit::EvolveOptions::Intensity::subtle, edit::EvolveOptions::Intensity::moderate, edit::EvolveOptions::Intensity::wild })
    {
        edit::EvolveOptions o;
        o.intensity = intensity;
        o.seed = 1234;
        const auto a = edit::evolve (d, reg(), o);
        const auto b = edit::evolve (d, reg(), o);
        REQUIRE (a.ok);
        CHECK (saveToString (a.doc) == saveToString (b.doc));   // same seed, same result
        CHECK_FALSE (a.changes.empty());
        o.seed = 99;
        CHECK (saveToString (edit::evolve (d, reg(), o).doc) != saveToString (a.doc));
        Validator v (reg());
        CHECK_FALSE (v.validate (a.doc).hasErrors());
        // levels preserved
        for (const auto& n : a.doc.nodes)
            if (const auto* orig = d.findNode (n.id))
                for (const auto& [k, value] : n.params)
                    if (k == "level" || k == "master" || k == "gain")
                        CHECK (value == orig->params.at (k));
    }
    // Protected modules are never touched.
    edit::EvolveOptions o;
    o.intensity = edit::EvolveOptions::Intensity::wild;
    o.seed = 5;
    const auto osc = test::nodeOfType (d, "osc.analog");
    o.protect = { osc };
    const auto p = edit::evolve (d, reg(), o);
    REQUIRE (p.ok);
    CHECK (p.doc.findNode (osc)->params == d.findNode (osc)->params);
    // One undo restores the original.
    edit::History h;
    Document live = d;
    h.push (live, "EVOLVE");
    live = p.doc;
    REQUIRE (h.undo (live));
    CHECK (saveToString (live) == original);
}

TEST_CASE ("Scenes: capture, recall, interpolate; auto-arrange flows left to right", "[unit][edit]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    edit::setParam (d, reg(), osc, "width", 0.2);
    edit::setParam (d, reg(), osc, "wave", 0);
    edit::captureScene (d, reg(), 0, "A");
    edit::setParam (d, reg(), osc, "width", 0.8);
    edit::setParam (d, reg(), osc, "wave", 2);
    edit::captureScene (d, reg(), 2, "C");
    edit::applyScenePosition (d, reg(), 1.0);   // halfway between I and III (II is empty)
    CHECK (d.findNode (osc)->params["width"] == Catch::Approx (0.5));
    CHECK (d.findNode (osc)->params["wave"] == 2.0);   // a choice switches at the middle
    REQUIRE (edit::recallScene (d, reg(), 0));
    CHECK (d.findNode (osc)->params["width"] == Catch::Approx (0.2));
    edit::autoArrange (d, reg());
    CHECK (d.findNode (test::nodeOfType (d, "event.keyboard"))->x < d.findNode (osc)->x);
    CHECK (d.findNode (osc)->x < d.findNode (test::nodeOfType (d, "util.output"))->x);
}
