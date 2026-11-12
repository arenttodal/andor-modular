#include "TestUtil.h"

#include "nodes/AnalogFx.h"

#include "fx/Effects.h"
#include "fx/FxParameterTable.h"
#include "fx/FxProcessor.h"

#include "core/Prng.h"

#include <set>

// The effect modules carry the whole of ANDOR ANALOG's FX library table (state schema 2):
// docs/ADDENDUM_02_REUSE_AUDIT.md, section 4.

using namespace test;
namespace afx = andor::analog::fx;

namespace
{
    const char* const fxModules[] { "fx.phaser", "fx.flanger", "fx.shifter", "fx.compressor", "fx.filter", "fx.retro", "fx.spread", "fx.convolution" };

    const ParamDesc* paramOf (const NodeDesc& d, const std::string& id)
    {
        const int i = d.paramIndex (id);
        return i >= 0 ? &d.params[static_cast<std::size_t> (i)] : nullptr;
    }
}

TEST_CASE ("Effect modules: every setting of the library's table is a parameter; the ones they had keep their place", "[unit][nodes][fx][table]")
{
    // What each module's parameters were before the table (schema 1), in order: saved patches,
    // macros and host parameters name them.
    const std::map<std::string, std::vector<std::string>> before {
        { "fx.phaser", { "mode", "rate", "depth", "centre", "feedback", "stages", "mix" } },
        { "fx.flanger", { "mode", "rate", "depth", "delay", "feedback", "mix" } },
        { "fx.shifter", { "mode", "shift", "down", "width", "feedback", "mix" } },
        { "fx.compressor", { "mode", "threshold", "ratio", "attack", "release", "makeup", "mix" } },
        { "fx.filter", { "type", "hz", "resonance", "drive", "mix" } },
        { "fx.retro", { "mode", "character", "wear", "saturation", "noise", "bits", "mix" } },
        { "fx.spread", { "mode", "width", "detune", "mix" } },
        { "fx.convolution", { "impulse", "mix", "predelay", "width", "lowcut", "highcut", "decay" } },
    };
    for (const auto* type : fxModules)
    {
        INFO (type);
        const auto* d = reg().find (type);
        REQUIRE (d != nullptr);
        afx::Type fx {};
        REQUIRE (analogFxType (type, fx));
        const auto& was = before.at (type);
        REQUIRE (d->params.size() > was.size());
        for (std::size_t i = 0; i < was.size(); ++i)
        {
            CHECK (d->params[i].id == was[i]);
            CHECK_FALSE (d->params[i].detail);
        }
        // Every setting but ON has one parameter; no parameter carries two.
        const auto& links = analogFxLinks (type);
        std::set<std::string> settings, params;
        for (const auto& l : links)
        {
            INFO (l.setting);
            CHECK (settings.insert (l.setting).second);
            CHECK (params.insert (l.param).second);
            const auto* p = paramOf (*d, l.param);
            REQUIRE (p != nullptr);
            // The parameter reaches the setting's range (a module's own may reach a little further).
            const auto& specs = afx::parameterSpecs (fx);
            const auto spec = std::find_if (specs.begin(), specs.end(), [&] (const afx::ParameterSpec& s) { return s.name == l.setting; });
            REQUIRE (spec != specs.end());
            CHECK (p->min * l.scale <= static_cast<double> (spec->low) + 1.0e-6);
            CHECK (p->max * l.scale >= static_cast<double> (spec->high) - 1.0e-6);
        }
        for (const auto& spec : afx::parameterSpecs (fx))
            if (spec.name != "on")
                CHECK (settings.count (spec.name) == 1);
        CHECK (params.size() == d->params.size());
        // The new ones are details: off the tile, left alone by EVOLVE.
        for (std::size_t i = was.size(); i < d->params.size(); ++i)
        {
            CHECK (d->params[i].detail);
            CHECK_FALSE (d->params[i].quick);
        }
    }
    CHECK_FALSE (analogFxLinks ("fx.echo").size() > 0);
    afx::Type none {};
    CHECK_FALSE (analogFxType ("proc.filter", none));
}

TEST_CASE ("Effect modules: the new settings at their defaults leave the sound as it was", "[unit][nodes][fx][table][regression]")
{
    // Before the table a module wrote only its own parameters and the processor played the rest
    // at the library's built-in values. Each new parameter's default must be that value: the
    // same input through the processor with and without them written is the same output.
    for (const auto* type : fxModules)
    {
        if (std::string (type) == "fx.convolution")
            continue;   // (its impulse is built from the settings: checked through the module below)
        INFO (type);
        const auto* d = reg().find (type);
        afx::Type fx {};
        REQUIRE (analogFxType (type, fx));
        afx::Settings plain, written;
        for (const auto& l : analogFxLinks (type))
        {
            const auto* p = paramOf (*d, l.param);
            const auto& specs = afx::parameterSpecs (fx);
            const auto& spec = *std::find_if (specs.begin(), specs.end(), [&] (const afx::ParameterSpec& s) { return s.name == l.setting; });
            const auto value = static_cast<float> (p->def * l.scale);
            spec.write (written, 0, value);
            if (! p->detail)
                spec.write (plain, 0, value);   // (a parameter the module always had: written in both)
        }
        auto a = afx::makeProcessor (fx), b = afx::makeProcessor (fx);
        a->prepare (48000.0, 256);
        b->prepare (48000.0, 256);
        osp::Prng noise (11);
        double difference = 0.0, level = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            std::array<float, 256> l1 {}, r1 {}, l2 {}, r2 {};
            for (std::size_t i = 0; i < 256; ++i)
            {
                l1[i] = l2[i] = static_cast<float> (0.5 * (2.0 * noise.nextDouble() - 1.0));
                r1[i] = r2[i] = static_cast<float> (0.5 * (2.0 * noise.nextDouble() - 1.0));
            }
            a->set (plain, 0);
            a->process (l1.data(), r1.data(), 256);
            b->set (written, 0);
            b->process (l2.data(), r2.data(), 256);
            for (std::size_t i = 0; i < 256; ++i)
            {
                difference = std::max ({ difference, static_cast<double> (std::abs (l1[i] - l2[i])), static_cast<double> (std::abs (r1[i] - r2[i])) });
                level = std::max (level, static_cast<double> (std::abs (l1[i])));
            }
        }
        CHECK (level > 0.01);
        // (To a float's rounding: the table counts 12 % as 0.01f * 12, the built-in value is
        // 0.12f. The module itself does not write a setting left at its default, so a schema 1
        // patch plays bit for bit as it did: dsp/nodes/FxNodes.cpp, writeExtras.)
        CHECK (difference < 1.0e-4);
    }
}

TEST_CASE ("Effect modules: a new setting is heard; MULTIBAND and the FET's own input act", "[unit][nodes][fx][table][dsp]")
{
    auto renderWith = [] (const std::string& type, std::map<std::string, double> params) {
        Document d = basicPatch();
        const auto mix = nodeOfType (d, "util.voicemix");
        const auto fx = edit::addModule (d, reg(), type, 600, 0, mix).focus;   // (inserted after VOICE MIX)
        REQUIRE (! fx.empty());
        for (const auto& [k, v] : params)
            d.findNode (fx)->params[k] = v;
        OfflineRenderer r;
        return render (r, d, { { 48, 0.0, 0.8 }, { 55, 0.0, 0.8 }, { 64, 0.0, 0.8 } }, 1.0);
    };
    auto differ = [] (const RenderResult& a, const RenderResult& b) {
        double sum = 0.0;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
            sum += std::abs (static_cast<double> (a.left[i] - b.left[i]));
        return sum;
    };
    // COMPRESSOR: MULTIBAND is another machine than CLEAN; FET's input drives it harder.
    const auto clean = renderWith ("fx.compressor", { { "mode", 0.0 }, { "threshold", -30.0 } });
    const auto multi = renderWith ("fx.compressor", { { "mode", 2.0 }, { "multi.depth", 1.0 } });
    CHECK (differ (clean, multi) > 1.0);
    const auto fetLow = renderWith ("fx.compressor", { { "mode", 1.0 }, { "fet.input", 0.0 } });
    const auto fetHigh = renderWith ("fx.compressor", { { "mode", 1.0 }, { "fet.input", 36.0 } });
    CHECK (differ (fetLow, fetHigh) > 1.0);
    // PHASER: negative feedback (the library's range) is not positive feedback.
    CHECK (differ (renderWith ("fx.phaser", { { "feedback", 0.7 } }), renderWith ("fx.phaser", { { "feedback", -0.7 } })) > 1.0);
    // RETRO: a machine page's setting (VINYL's crackle) is heard.
    CHECK (differ (renderWith ("fx.retro", { { "mode", 1.0 }, { "crackle", 0.0 } }), renderWith ("fx.retro", { { "mode", 1.0 }, { "crackle", 1.0 } })) > 0.1);
    // CONVOLUTION: REVERSE rebuilds the impulse.
    CHECK (differ (renderWith ("fx.convolution", { { "reverse", 0.0 } }), renderWith ("fx.convolution", { { "reverse", 1.0 } })) > 1.0);
    // ... and at their defaults the new settings of CONVOLUTION are the impulse it always built.
    const auto conv = renderWith ("fx.convolution", {});
    CHECK (rms (conv.left) > 0.005);
}

TEST_CASE ("Schema 1 patches migrate to schema 2: mappings and modulation reach the values they did", "[unit][graph][state][fx]")
{
    // tests/fixtures/patch-v1-fx.json: a schema 1 patch whose macros and modulation act on
    // parameters that grew to the library's ranges.
    Document d;
    const auto res = loadFromFile (std::string (MODULAR_SOURCE_DIR) + "/tests/fixtures/patch-v1-fx.json", d);
    REQUIRE (res.ok);
    CHECK (res.migratedFrom == 1);

    // Values in their own units are untouched.
    CHECK (d.findNode ("comp")->param ("threshold", 0.0) == -30.0);
    CHECK (d.findNode ("comp")->param ("mode", 0.0) == 1.0);
    CHECK (d.findNode ("ph")->param ("centre", 0.0) == 1200.0);
    // COMPRESSOR's FET input followed THRESHOLD; it starts there.
    CHECK (d.findNode ("comp")->param ("fet.input", 0.0) == 30.0);
    CHECK (d.findNode ("comp2")->param ("fet.input", -1.0) == 0.0);

    auto mapping = [&] (int macro, const std::string& node, const std::string& param) -> const MacroMapping& {
        for (const auto& m : d.macros[static_cast<std::size_t> (macro)].mappings)
            if (m.node == node && m.param == param)
                return m;
        FAIL ("no mapping " << node << "/" << param);
        static MacroMapping none;
        return none;
    };
    auto native = [&] (const std::string& node, const std::string& param, double n) {
        const auto* desc = reg().find (d.findNode (node)->type);
        return paramOf (*desc, param)->denormalise (n);
    };
    // PHASER CENTRE was 100..5000 Hz (logarithmic): 0.25..0.75 of it was 266..1880 Hz.
    {
        const auto& m = mapping (0, "ph", "centre");
        CHECK (native ("ph", "centre", m.min) == Catch::Approx (100.0 * std::pow (50.0, 0.25)).epsilon (1.0e-9));
        CHECK (native ("ph", "centre", m.max) == Catch::Approx (100.0 * std::pow (50.0, 0.75)).epsilon (1.0e-9));
    }
    // PHASER FEEDBACK was 0..0.95.
    {
        const auto& m = mapping (0, "ph", "feedback");
        CHECK (native ("ph", "feedback", m.min) == Catch::Approx (0.0).margin (1.0e-9));
        CHECK (native ("ph", "feedback", m.max) == Catch::Approx (0.95).epsilon (1.0e-9));
    }
    // COMPRESSOR THRESHOLD was -48..0 dB, MAKEUP 0..24 dB, MODE two positions (inverted: FET down to CLEAN).
    {
        const auto& t = mapping (1, "comp", "threshold");
        CHECK (native ("comp", "threshold", t.min) == Catch::Approx (-36.0).epsilon (1.0e-9));
        CHECK (native ("comp", "threshold", t.max) == Catch::Approx (-12.0).epsilon (1.0e-9));
        const auto& k = mapping (1, "comp", "makeup");
        CHECK (native ("comp", "makeup", k.min) == Catch::Approx (0.0).margin (1.0e-9));
        CHECK (native ("comp", "makeup", k.max) == Catch::Approx (12.0).epsilon (1.0e-9));
        const auto& mode = mapping (1, "comp", "mode");
        CHECK (native ("comp", "mode", mode.min) == 0.0);
        CHECK (native ("comp", "mode", mode.max) == 1.0);   // (never MULTIBAND: the patch could not mean it)
    }
    // A group's control onto CONVOLUTION's WIDTH (was 0..1) and LOW CUT (20..1000 Hz, logarithmic).
    {
        REQUIRE (d.groups.size() == 1);
        REQUIRE (d.groups[0].controls.size() == 1);
        const auto& maps = d.groups[0].controls[0].mappings;
        REQUIRE (maps.size() == 2);
        CHECK (native ("conv", "width", maps[0].max) == Catch::Approx (1.0).epsilon (1.0e-9));
        CHECK (native ("conv", "lowcut", maps[1].max) == Catch::Approx (1000.0).epsilon (1.0e-9));
        CHECK (native ("conv", "lowcut", maps[1].min) == Catch::Approx (20.0).epsilon (1.0e-9));
    }
    // An LFO into FLANGER DELAY at depth 0.1 swung a tenth of 0.5..10 ms: the same milliseconds now.
    // (A modulation deep enough to hit the old range's end would now carry on to the new end:
    // the one thing the migration cannot keep.)
    const auto modulation = [&] (const std::string& node, const std::string& param) -> const Connection* {
        for (const auto& c : d.connections)
            if (c.toParam && c.to == node && c.toPort == param)
                return &c;
        return nullptr;
    };
    {
        const auto* lfo = modulation ("fl", "delay");
        REQUIRE (lfo != nullptr);
        CHECK (lfo->depth * (12.0 - 0.2) == Catch::Approx (0.1 * (10.0 - 0.5)).epsilon (1.0e-9));
    }
    // COMPRESSOR's FET input followed THRESHOLD live: whatever moved THRESHOLD moves it the same
    // way now - the macro (the other direction: input = -threshold), the LFO, the scene.
    {
        const auto& t = mapping (1, "comp", "threshold");
        const auto& f = mapping (1, "comp", "fet.input");
        CHECK (native ("comp", "fet.input", f.min) == Catch::Approx (-native ("comp", "threshold", t.min)).epsilon (1.0e-9));
        CHECK (native ("comp", "fet.input", f.max) == Catch::Approx (-native ("comp", "threshold", t.max)).epsilon (1.0e-9));
        CHECK (f.curve == t.curve);
        const auto* onThreshold = modulation ("comp", "threshold");
        const auto* onInput = modulation ("comp", "fet.input");
        REQUIRE (onThreshold != nullptr);
        REQUIRE (onInput != nullptr);
        CHECK (onThreshold->depth * 60.0 == Catch::Approx (0.1 * 48.0).epsilon (1.0e-9));   // (-48..0 dB then, -60..0 dB now)
        CHECK (onInput->depth == Catch::Approx (-onThreshold->depth).epsilon (1.0e-12));
        CHECK (onInput->from == onThreshold->from);
        CHECK (onInput->id != onThreshold->id);
        REQUIRE (d.scenes[0].captured);
        CHECK (d.scenes[0].values.at ("comp/threshold") == -40.0);
        CHECK (d.scenes[0].values.at ("comp/fet.input") == 40.0);
    }
    // A parameter whose range did not change keeps its mapping.
    CHECK (mapping (0, "ph", "mix").max == 0.8);

    // It plays, saves as schema 2, and a schema 2 patch is read as it is.
    OfflineRenderer r;
    CHECK (rms (render (r, d, { { 60, 0.0, 0.3 } }, 0.5).left) > 0.005);
    const auto saved = saveToString (d);
    CHECK (saved.find ("\"schemaVersion\": 2") != std::string::npos);
    Document again;
    const auto second = loadFromString (saved, again);
    REQUIRE (second.ok);
    CHECK (second.migratedFrom == 0);
    CHECK (second.warnings.empty());
    CHECK (saveToString (again) == saved);
}
