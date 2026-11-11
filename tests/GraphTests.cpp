#include "TestUtil.h"

#include <fstream>

using namespace andor::modular;
using test::reg;

TEST_CASE ("Every module type is registered completely", "[unit][graph]")
{
    REQUIRE (reg().all().size() >= 45);
    for (const auto* d : reg().all())
    {
        INFO (d->type);
        CHECK_FALSE (d->name.empty());
        CHECK_FALSE (d->description.empty());
        CHECK (d->supports (d->defaultDomain));
        CHECK (d->inputs.size() <= static_cast<std::size_t> (maxPorts));
        CHECK (d->outputs.size() <= static_cast<std::size_t> (maxPorts));
        CHECK (d->params.size() <= 64);
        if (d->available)
            CHECK ((d->create != nullptr || d->createEvent != nullptr));
        else
            CHECK_FALSE (d->unavailableReason.empty());
        for (const auto& p : d->params)
        {
            INFO (p.id);
            CHECK (p.def >= std::min (p.min, p.max));
            CHECK (p.def <= std::max (p.min, p.max));
            CHECK (std::abs (p.denormalise (p.normalise (p.def)) - p.def) < 1.0e-6 * std::max (1.0, std::abs (p.def)));
            if (p.structural)
                CHECK_FALSE (p.modulatable);
        }
    }
}

TEST_CASE ("Quick Add search finds modules by name, keyword and fuzzy typing", "[unit][graph]")
{
    CHECK (reg().search ("analog").front()->type == "osc.analog");
    CHECK (reg().search ("filter").front()->type == "proc.filter");
    CHECK (reg().search ("reverb").front()->type == "fx.space");
    CHECK (reg().search ("arp").front()->type == "event.arp");
    CHECK (reg().search ("anlg osc").front()->type == "osc.analog");
    CHECK (reg().search ("zzzzqq").empty());
}

TEST_CASE ("The validator enforces types, domains, fan-in, cycles and budgets", "[unit][graph]")
{
    std::string osc;
    Document d = test::basicPatch (&osc);
    Validator v (reg());
    CHECK_FALSE (v.validate (d).hasErrors());
    const auto vca = test::nodeOfType (d, "proc.vca");
    const auto mix = test::nodeOfType (d, "util.voicemix");
    const auto out = test::nodeOfType (d, "util.output");

    // voice audio straight into a global module: refused, VOICE MIX suggested
    auto check = v.checkConnection (d, vca, "out", out, "in", false);
    CHECK_FALSE (check.ok);
    CHECK (check.suggestion == "util.voicemix");
    // notes into audio
    CHECK_FALSE (v.checkConnection (d, test::nodeOfType (d, "event.keyboard"), "notes", vca, "in", false).ok);
    // fan-in on a single input
    auto* osc2 = d.addNode (reg(), "osc.analog", 0, 300);
    check = v.checkConnection (d, osc2->id, "out", vca, "in", false);
    CHECK_FALSE (check.ok);
    CHECK (check.suggestion == "util.voicemix");
    // a loop without FEEDBACK DELAY
    auto* f1 = d.addNode (reg(), "proc.tone", 900, 0);
    auto* f2 = d.addNode (reg(), "proc.drive", 900, 200);
    const auto f1id = f1->id, f2id = f2->id;
    d.connect (f1id, "out", f2id, "in");
    CHECK (v.wouldCycle (d, f2id, f1id));
    CHECK_FALSE (v.checkConnection (d, f2id, "out", f1id, "in", false).ok);
    // ...with one: allowed
    auto* fb = d.addNode (reg(), "util.feedback", 900, 400);
    const auto fbid = fb->id;
    d.connect (f2id, "out", fbid, "in");
    CHECK (v.checkConnection (d, fbid, "out", f1id, "in", false).ok);
    // global control into a voice parameter: broadcast
    auto* lfo = d.addNode (reg(), "ctl.lfo", -200, 300);
    check = v.checkConnection (d, lfo->id, "out", osc, "fine", true);
    CHECK (check.ok);
    CHECK (check.implicitBroadcast);
    // a per-voice control into a global parameter: refused (POLY > GLOBAL)
    auto* vlfo = d.addNode (reg(), "ctl.lfo", -200, 500);
    vlfo->domain = Domain::voice;
    check = v.checkConnection (d, vlfo->id, "out", mix, "level", true);
    CHECK_FALSE (check.ok);
    CHECK (check.suggestion == "util.polyglobal");
}

TEST_CASE ("Budgets: a seventh source is refused with a reason", "[unit][graph]")
{
    Document d;
    for (int i = 0; i < 6; ++i)
        REQUIRE (edit::addModule (d, reg(), "osc.analog", 0, 150.0f * static_cast<float> (i)).ok);
    const auto r = edit::addModule (d, reg(), "src.noise", 0, 1000);
    CHECK_FALSE (r.ok);
    CHECK (r.message.find ("6 sound sources") != std::string::npos);
    Validator v (reg());
    CHECK (v.validate (d).sources == 6);
}

TEST_CASE ("Save and load are lossless and deterministic; missing modules survive", "[unit][graph][state]")
{
    Document d = test::basicPatch();
    d.macros[0].name = "BRIGHT";
    edit::mapMacro (d, reg(), 0, test::nodeOfType (d, "osc.analog"), "width", 0.1, 0.9);
    edit::captureScene (d, reg(), 1, "TWO");
    d.nodes.push_back ({ "n-ghost", "future.module", 3, Domain::global, 10, 20, 1, true, "", "", { { "x", 1.5 } }, { { "sample", "a.wav" } } });
    const auto text = saveToString (d);
    Document back;
    const auto res = loadFromString (text, back);
    REQUIRE (res.ok);
    CHECK (saveToString (back) == text);
    REQUIRE (back.findNode ("n-ghost") != nullptr);
    CHECK (back.findNode ("n-ghost")->params.at ("x") == 1.5);
    CHECK (back.macros[0].mappings.size() == 1);
    CHECK (back.scenes[1].captured);

    // a missing module compiles inert, everything else still plays
    OfflineRenderer r;
    const auto out = test::render (r, back, { { 69, 0.0, 0.5 } }, 0.6);
    CHECK (test::rms (out.left) > 0.01);

    // atomic save
    const std::string path = "modular-test-save.andormodular";
    std::string error;
    REQUIRE (saveToFile (d, path, error));
    Document fromFile;
    CHECK (loadFromFile (path, fromFile).ok);
    std::remove (path.c_str());
}

TEST_CASE ("Schema 0 patches migrate to schema 1", "[unit][graph][state]")
{
    const std::string v0 = R"({ "format": "andormodular", "name": "old",
        "nodes": [ { "id": "k", "type": "event.keyboard", "pos": [0, 0] },
                   { "id": "o", "type": "osc.analog", "domain": "voice", "pos": [200, 10] },
                   { "id": "m", "type": "util.voicemix", "pos": [400, 0] },
                   { "id": "out", "type": "util.output", "pos": [600, 0] } ],
        "edges": [ { "id": "e1", "src": "k:notes", "dst": "o:notes" }, { "id": "e2", "src": "o:out", "dst": "m:in" },
                   { "id": "e3", "src": "m:out", "dst": "out:in" } ] })";
    Document d;
    const auto res = loadFromString (v0, d);
    REQUIRE (res.ok);
    CHECK (res.warnings.front().find ("schema 0") != std::string::npos);
    CHECK (d.connections.size() == 3);
    CHECK (d.findNode ("o")->x == 200.0f);
    OfflineRenderer r;
    CHECK (test::rms (test::render (r, d, { { 60, 0.0, 0.3 } }, 0.4).left) > 0.01);
    // a newer schema is refused, not misread
    Document n;
    CHECK_FALSE (loadFromString (R"({"format":"andormodular","schemaVersion":99})", n).ok);
}

TEST_CASE ("The compiler forms independent lanes and orders units", "[unit][graph]")
{
    Document d;
    const auto a = edit::addModule (d, reg(), "osc.analog", 0, 0).focus;
    const auto b = edit::addModule (d, reg(), "osc.analog", 0, 200).focus;
    CompileOptions o;
    auto g = compileGraph (d, reg(), o);
    CHECK (g->lanes.size() == 2);   // two chains, two note streams' worth of voices
    // a per-voice MORPH joins them into one lane (both play the same note)
    REQUIRE (edit::dropOnSource (d, reg(), b, a, edit::DropAction::morph).ok);
    g = compileGraph (d, reg(), o);
    CHECK (g->lanes.size() == 1);
    CHECK (g->outputNode >= 0);
    CHECK_FALSE (g->report.hasErrors());
}
