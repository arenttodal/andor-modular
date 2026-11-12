#include "ModuleRack.h"

#include "Canvas.h"
#include "FxIdentity.h"
#include "ParameterRack.h"
#include "PlayView.h"
#include "StepGrid.h"

#include "engine/DriveProcessor.h"
#include "fx/Effects.h"

#include <complex>

namespace andor::modular::ui
{

namespace
{
    namespace an = andor::analog;
    namespace rackui = osp::plugin::rackui;
    using osp::plugin::MiniKnob;
    using osp::plugin::rack::Faceplate;
    using osp::plugin::rack::GraphFrame;
    using osp::plugin::rack::ParameterRack;

    juce::String rackId (const std::string& param) { return "m." + juce::String (param); }

    /** A rack shows percent where the module counts 0..1 (or -1..1). */
    bool asPercent (const ParamDesc& p) { return p.unit == "%" && p.curve != ParamCurve::integer && p.max <= 1.0001 && p.min >= -1.0001; }

    /** The rack's schema for a module: one parameter for each of its own, in the family's units,
        and its ON (the module's light). */
    RackHost::Layout layoutFor (const NodeDesc& d, std::vector<RackParam>& binding)
    {
        RackHost::Layout layout;
        using Range = juce::NormalisableRange<float>;
        for (const auto& p : d.params)
        {
            const auto id = rackId (p.id);
            const juce::ParameterID pid { id, 1 };
            const auto name = juce::String (p.name);
            if (p.curve == ParamCurve::choice)
            {
                juce::StringArray items;
                for (const auto& c : p.choices)
                    items.add (juce::String (c));
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, name, items, juce::jlimit (0, std::max (0, items.size() - 1), juce::roundToInt (p.def))));
                binding.push_back ({ id, p.id, 1.0, 0.0, {}, {}, {}, {} });
            }
            else if (p.curve == ParamCurve::toggle)
            {
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, name, p.def >= 0.5));
                binding.push_back ({ id, p.id, 1.0, 0.0, {}, {}, {}, {} });
            }
            else
            {
                const double k = asPercent (p) ? 100.0 : 1.0;
                Range range (static_cast<float> (p.min * k), static_cast<float> (p.max * k), p.curve == ParamCurve::integer ? 1.0f : 0.0f);
                if (p.curve == ParamCurve::logarithmic && p.min > 0.0)
                    range.setSkewForCentre (static_cast<float> (std::sqrt (p.min * p.max)));
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, name, range, static_cast<float> (p.def * k)));
                binding.push_back ({ id, p.id, k, 0.0, {}, {}, {}, {} });
            }
        }
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "m.on", 1 }, "On", true));
        return layout;
    }

    /** The cables that modulate this module's parameters, in the patch's order. */
    std::vector<std::string> modulationsOf (const Document& doc, const std::string& id)
    {
        std::vector<std::string> ids;
        for (const auto& c : doc.connections)
            if (c.toParam && c.to == id)
                ids.push_back (c.id);
        return ids;
    }

    //==============================================================================================
    // The pictures.
    float valueOf (Session& s, const std::string& id, const char* param) { return static_cast<float> (s.document().paramValue (s.registry(), id, param)); }

    const CompiledNode* liveOf (Session& s, const std::string& id)
    {
        if (const auto* g = s.graph())
            if (const auto it = g->nodeIndexById.find (id); it != g->nodeIndexById.end())
                return &g->nodes[static_cast<std::size_t> (it->second)];
        return nullptr;
    }

    float telemetryOf (const CompiledNode* n, int i) { return n != nullptr && i < n->telemetryCount.load() ? n->telemetry[static_cast<std::size_t> (i)].load() : 0.0f; }

    /** The glass's small words (the family's: 9.5 px, tracked). */
    void caption (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Justification where, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (fonts::make (9.5f, Weight::medium, 0.1f));
        g.drawText (text, area.reduced (10.0f, 7.0f), where, false);
    }

    /** A curve on the glass: `at (t)` for t in 0..1 gives 0..1 up the area; its line in the
        device's light with a soft fill under it. */
    void curve (juce::Graphics& g, juce::Rectangle<float> plot, const std::function<float (float)>& at, juce::Colour line, int points = 160, bool fill = true)
    {
        juce::Path p;
        for (int i = 0; i < points; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (points - 1);
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, at (t));
            if (i == 0)
                p.startNewSubPath (plot.getX(), y);
            else
                p.lineTo (plot.getX() + plot.getWidth() * t, y);
        }
        if (fill)
        {
            juce::Path under (p);
            under.lineTo (plot.getRight(), plot.getBottom());
            under.lineTo (plot.getX(), plot.getBottom());
            under.closeSubPath();
            g.setGradientFill (juce::ColourGradient (line.withAlpha (0.20f), 0.0f, plot.getY(), line.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
            g.fillPath (under);
        }
        g.setColour (line);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void grid (juce::Graphics& g, juce::Rectangle<float> plot, int columns, int rows)
    {
        g.setColour (colour::wellGrid.withAlpha (0.55f));
        for (int i = 1; i < columns; ++i)
            g.fillRect (juce::Rectangle<float> (plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (columns), plot.getY(), 1.0f, plot.getHeight()));
        for (int i = 1; i < rows; ++i)
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY() + plot.getHeight() * static_cast<float> (i) / static_cast<float> (rows), plot.getWidth(), 1.0f));
    }

    juce::Rectangle<float> plotIn (juce::Rectangle<float> area) { return area.reduced (12.0f, 22.0f); }
    juce::Colour words() { return colour::wellText.brighter (0.25f); }

    /** FILTER: the small-signal response of the model that plays, at this module's settings.
        Ladder and OTA: ANDOR Analog's own linearisation (AnalogRacks.cpp, filtergraph::responseDb:
        H = G^4 / (1 + k G^4), G = 1 / (1 + j f / fc); the ladder's k = 4.15 RESO, the OTA's from
        its calibration with its pass-band restore, JUPITER 12 tapping the second stage). The
        12 dB modes: the module's state-variable filter (damping 2 - 1.96 RESO). */
    double filterResponseDb (int mode, double cutoffHz, double resonance, double hz)
    {
        using C = std::complex<double>;
        const double w = hz / std::max (1.0, cutoffHz), r = std::clamp (resonance, 0.0, 1.0);
        C h;
        if (mode <= 3)
        {
            const C G = 1.0 / (1.0 + C (0.0, w)), G2 = G * G, G4 = G2 * G2;
            if (mode == 0)
                h = G4 / (1.0 + 4.15 * r * G4);
            else
            {
                const bool jupiter = mode >= 2;
                const double maxK = jupiter ? 4.6 : 4.2, taper = jupiter ? 0.85 : 0.6, comp = jupiter ? 0.4 : 0.25;
                const double k = maxK * r * (taper + (1.0 - taper) * r);
                h = (1.0 + comp * k) * (mode == 3 ? G2 : G4) / (1.0 + k * G4);
            }
        }
        else
        {
            const double k = 2.0 - 1.96 * r;
            const C s (0.0, w), den = s * s + k * s + 1.0;
            h = mode == 4 ? 1.0 / den : (mode == 5 ? s * s / den : (mode == 6 ? k * s / den : (s * s + 1.0) / den));
        }
        return 20.0 * std::log10 (std::max (1.0e-6, std::abs (h)));
    }

    void paintFilter (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 3, 4);
        const int mode = juce::roundToInt (valueOf (s, id, "mode"));
        const double cutoff = valueOf (s, id, "cutoff"), reso = valueOf (s, id, "resonance");
        // 20 Hz .. 20 kHz, +18 .. -42 dB
        const auto xOf = [plot] (double hz) { return plot.getX() + plot.getWidth() * static_cast<float> (std::log (hz / 20.0) / std::log (1000.0)); };
        curve (g, plot, [&] (float t) { return static_cast<float> ((filterResponseDb (mode, cutoff, reso, 20.0 * std::pow (1000.0, static_cast<double> (t))) + 42.0) / 60.0); }, look.trace);
        g.setColour (look.trace.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (xOf (std::clamp (cutoff, 20.0, 20000.0)), plot.getY(), 1.0f, plot.getHeight()));
        const auto* n = s.document().findNode (id);
        caption (g, area, juce::String ("RESPONSE  ") + (n != nullptr && n->domain == Domain::voice ? "AT REST, PER VOICE" : "GLOBAL"), juce::Justification::topLeft, words());
        caption (g, area, juce::String (s.registry().find ("proc.filter")->params[1].format (cutoff)).toUpperCase(), juce::Justification::topRight, look.trace);
        caption (g, area, "20 Hz", juce::Justification::bottomLeft, words().withAlpha (0.6f));
        caption (g, area, "20 kHz", juce::Justification::bottomRight, words().withAlpha (0.6f));
    }

    /** FX FILTER: ANDOR Analog's FxFilter computes its own response (as its rack's graph does). */
    void paintFxFilter (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 3, 4);
        an::fx::FxFilter model;
        model.prepare (48000.0, 64);
        an::fx::Settings all;
        auto& fs = all.filter[0];
        fs.type = juce::roundToInt (valueOf (s, id, "type"));
        fs.hz = valueOf (s, id, "hz");
        fs.resonance = valueOf (s, id, "resonance");
        fs.driveDb = valueOf (s, id, "drive");
        model.set (all, 0);
        model.reset();
        curve (g, plot, [&] (float t) { return static_cast<float> ((model.responseDb (20.0 * std::pow (1000.0, static_cast<double> (t))) + 42.0) / 60.0); }, look.trace);
        caption (g, area, "RESPONSE", juce::Justification::topLeft, words());
        caption (g, area, "20 Hz", juce::Justification::bottomLeft, words().withAlpha (0.6f));
        caption (g, area, "20 kHz", juce::Justification::bottomRight, words().withAlpha (0.6f));
    }

    /** DRIVE: the circuit's steady-state transfer curve (DriveProcessor::transfer, as ANDOR's
        DRIVE graph draws it), in to out, with the straight line of no drive behind it. */
    void paintDrive (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area).withSizeKeepingCentre (std::min (plotIn (area).getWidth(), plotIn (area).getHeight() * 1.8f), plotIn (area).getHeight());
        grid (g, plot, 4, 4);
        osp::DriveProcessor::Settings ds;
        ds.amount = valueOf (s, id, "amount");
        ds.mode = static_cast<osp::DriveMode> (juce::jlimit (0, 2, juce::roundToInt (valueOf (s, id, "mode"))));
        ds.tone = valueOf (s, id, "tone");
        ds.body = valueOf (s, id, "body");
        g.setColour (words().withAlpha (0.35f));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);
        curve (g, plot, [&] (float t) { return 0.5f + 0.5f * static_cast<float> (osp::DriveProcessor::transfer (ds, 2.0 * t - 1.0)); }, look.trace, 200, false);
        caption (g, area, "TRANSFER  IN > OUT", juce::Justification::topLeft, words());
        caption (g, area, juce::String (juce::roundToInt (100.0f * valueOf (s, id, "amount"))) + " %", juce::Justification::topRight, look.trace);
    }

    /** COMPRESSOR: CLEAN's static curve (Compressor::cleanCurveDb, as ANDOR Analog's graph), the
        threshold on it; the level leaving the module now. */
    void paintCompressor (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area).withSizeKeepingCentre (std::min (plotIn (area).getWidth(), plotIn (area).getHeight() * 1.8f), plotIn (area).getHeight());
        grid (g, plot, 4, 4);
        an::fx::CompSettings cs;
        cs.mode = juce::roundToInt (valueOf (s, id, "mode"));
        cs.thresholdDb = valueOf (s, id, "threshold");
        cs.ratio = valueOf (s, id, "ratio");
        cs.makeupDb = valueOf (s, id, "makeup");
        // -60 .. 0 dB in, -60 .. 0 dB out
        g.setColour (words().withAlpha (0.35f));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);
        if (cs.mode == 0)
        {
            curve (g, plot, [&] (float t) { return (an::fx::Compressor::cleanCurveDb (cs, -60.0f + 60.0f * t) + 60.0f) / 60.0f; }, look.trace, 120, false);
            g.setColour (look.trace.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (plot.getX() + plot.getWidth() * (cs.thresholdDb + 60.0f) / 60.0f, plot.getY(), 1.0f, plot.getHeight()));
            caption (g, area, "CURVE  IN > OUT", juce::Justification::topLeft, words());
        }
        else
            caption (g, area, "FET: INPUT DRIVES IT INTO A FIXED THRESHOLD", juce::Justification::topLeft, words());
        // what it is doing now: the processor's own gain reduction, and what leaves it
        const auto* live = liveOf (s, id);
        const float peak = live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
        const float reduction = live != nullptr && live->telemetryCount.load() > 0 ? std::abs (telemetryOf (live, 0)) : 0.0f;
        const auto bar = juce::Rectangle<float> (area.getX() + 12.0f, area.getBottom() - 30.0f, 110.0f, 5.0f);
        g.setColour (colour::wellGrid);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (look.trace);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, reduction / 24.0f)), 2.0f);
        caption (g, area, "GAIN REDUCTION  " + juce::String (reduction, 1) + " dB", juce::Justification::bottomLeft, words());
        caption (g, area, peak > 0.0005f ? "OUT " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB" : juce::String ("SILENT"), juce::Justification::bottomRight, look.trace);
    }

    /** An envelope's shape from its own times and level (to scale: the whole of A + D + a held
        sustain + R), and where it is now. */
    void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 4, 2);
        const float a = valueOf (s, id, "attack"), d = valueOf (s, id, "decay"), sus = valueOf (s, id, "sustain"), r = valueOf (s, id, "release");
        const float hold = std::max (0.15f, 0.35f * (a + d + r)), total = std::max (0.001f, a + d + hold + r);
        const auto x = [plot, total] (float seconds) { return plot.getX() + plot.getWidth() * seconds / total; };
        const auto y = [plot] (float level) { return plot.getBottom() - plot.getHeight() * 0.94f * level; };
        juce::Path p;
        p.startNewSubPath (x (0.0f), y (0.0f));
        p.lineTo (x (a), y (1.0f));
        p.quadraticTo (x (a + 0.35f * d), y (sus + 0.15f * (1.0f - sus)), x (a + d), y (sus));
        p.lineTo (x (a + d + hold), y (sus));
        p.quadraticTo (x (a + d + hold + 0.35f * r), y (0.15f * sus), x (total), y (0.0f));
        juce::Path under (p);
        under.closeSubPath();
        g.setGradientFill (juce::ColourGradient (look.trace.withAlpha (0.22f), 0.0f, plot.getY(), look.trace.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
        g.fillPath (under);
        g.setColour (look.trace);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (const auto node : { juce::Point<float> (x (a), y (1.0f)), juce::Point<float> (x (a + d), y (sus)), juce::Point<float> (x (a + d + hold), y (sus)) })
        {
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (node));
        }
        // its level now (the newest voice's, as the module reports it)
        const auto* live = liveOf (s, id);
        const float level = live != nullptr && live->telemetryCount.load() > 0 ? std::abs (telemetryOf (live, 0)) : (live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f);
        g.setColour (look.trace.withAlpha (0.8f));
        g.fillRoundedRectangle (juce::Rectangle<float> (plot.getRight() - 4.0f, y (juce::jlimit (0.0f, 1.0f, level)), 4.0f, plot.getBottom() - y (juce::jlimit (0.0f, 1.0f, level))), 1.5f);
        caption (g, area, "A  D  S  R", juce::Justification::topLeft, words());
        caption (g, area, juce::String (a + d + r, 2) + " s", juce::Justification::topRight, words());
    }

    /** LFO: two cycles of its shape, and its value now. */
    void paintLfo (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 4, 2);
        const int shape = juce::roundToInt (valueOf (s, id, "shape"));
        curve (g, plot, [shape] (float t) {
            // SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE; S&H and SMOOTH: a fixed picture of their kind
            const float ph = std::fmod (t * 2.0f, 1.0f);
            float v = std::sin (juce::MathConstants<float>::twoPi * ph);
            if (shape == 1) v = 1.0f - 4.0f * std::abs (ph - 0.5f);
            else if (shape == 2) v = 2.0f * ph - 1.0f;
            else if (shape == 3) v = 1.0f - 2.0f * ph;
            else if (shape == 4) v = ph < 0.5f ? 1.0f : -1.0f;
            else if (shape >= 5)
            {
                static const float held[] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.6f, -0.9f, 0.1f, 0.75f, -0.45f };
                const float at = t * 8.0f;
                const int i = juce::jlimit (0, 7, static_cast<int> (at));
                const float f = at - static_cast<float> (i);
                v = shape == 5 ? held[i] : held[i] + (held[i + 1] - held[i]) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * f));
            }
            return 0.5f + 0.45f * v;
        }, look.trace, 240, false);
        const float now = telemetryOf (liveOf (s, id), 0);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ plot.getRight() - 6.0f, plot.getCentreY() - 0.45f * plot.getHeight() * juce::jlimit (-1.0f, 1.0f, now) }));
        caption (g, area, shape >= 5 ? "SHAPE  A PICTURE OF ITS KIND" : "SHAPE  2 CYCLES", juce::Justification::topLeft, words());
        caption (g, area, "NOW " + juce::String (juce::roundToInt (100.0f * now)) + " %", juce::Justification::topRight, look.trace);
    }

    /** SATELLITE: where the sound is between the speakers now (a pan position: left to right;
        ORBIT's "behind" is a level, not a place, and is not drawn as one). */
    void paintSatellite (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        const auto centre = plot.getCentre();
        const float rx = plot.getWidth() * 0.46f, ry = plot.getHeight() * 0.36f;
        g.setColour (colour::wellGrid);
        g.drawEllipse (juce::Rectangle<float> (2.0f * rx, 2.0f * ry).withCentre (centre), 1.0f);
        g.fillRect (juce::Rectangle<float> (plot.getX(), centre.y - 0.5f, plot.getWidth(), 1.0f));
        const float pan = juce::jlimit (-1.0f, 1.0f, telemetryOf (liveOf (s, id), 0));
        g.setColour (look.trace.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ centre.x + rx * pan, centre.y }));
        g.setColour (look.trace);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ centre.x + rx * pan, centre.y }));
        caption (g, area, "L", juce::Justification::centredLeft, words());
        caption (g, area, "R", juce::Justification::centredRight, words());
        caption (g, area, "POSITION NOW  LEFT > RIGHT", juce::Justification::topLeft, words());
    }

    void paintSource (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        paintSourceDisplay (g, area, s, id, look, true, false);
    }

    /** A level on the glass: a meter with its scale (-48 .. 0 dB), what leaves the module now. */
    void meter (juce::Graphics& g, juce::Rectangle<float> row, float gain, juce::Colour light)
    {
        const float db = juce::jlimit (-48.0f, 0.0f, juce::Decibels::gainToDecibels (gain, -100.0f));
        g.setColour (colour::wellGrid);
        g.fillRoundedRectangle (row, 2.5f);
        g.setColour (gain > 0.98f ? juce::Colour (0xffe2684a) : light);
        g.fillRoundedRectangle (row.withWidth (row.getWidth() * (db + 48.0f) / 48.0f), 2.5f);
        g.setColour (words().withAlpha (0.45f));
        g.setFont (fonts::make (8.5f, Weight::medium, 0.06f));
        for (const int mark : { -48, -36, -24, -12, -6, 0 })
        {
            const float x = row.getX() + row.getWidth() * static_cast<float> (mark + 48) / 48.0f;
            g.fillRect (juce::Rectangle<float> (std::min (x, row.getRight() - 1.0f), row.getBottom() + 3.0f, 1.0f, 4.0f));
            g.drawText (juce::String (mark), juce::Rectangle<float> (x - 16.0f, row.getBottom() + 8.0f, 32.0f, 11.0f), juce::Justification::centred, false);
        }
    }

    float peakOf (Session& s, const std::string& id)
    {
        const auto* live = liveOf (s, id);
        return live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
    }

    juce::String dbText (float gain) { return gain > 0.0005f ? juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB" : juce::String ("SILENT"); }

    /** Any module without a picture of its own: what leaves it now, on a scale. */
    void paintMeter (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto* n = s.document().findNode (id);
        const bool control = n != nullptr && (n->type.rfind ("ctl.", 0) == 0);
        const auto plot = plotIn (area);
        if (control)
        {
            // a control signal: its value now, -100 .. +100 %
            const float v = juce::jlimit (-1.0f, 1.0f, telemetryOf (liveOf (s, id), 0));
            const auto row = juce::Rectangle<float> (plot.getX(), plot.getCentreY() - 5.0f, plot.getWidth(), 10.0f);
            g.setColour (colour::wellGrid);
            g.fillRoundedRectangle (row, 2.5f);
            g.setColour (look.trace);
            const float mid = row.getCentreX(), x = mid + 0.5f * row.getWidth() * v;
            g.fillRoundedRectangle (juce::Rectangle<float> (std::min (mid, x), row.getY(), std::abs (x - mid), row.getHeight()), 2.5f);
            g.setColour (words());
            g.fillRect (juce::Rectangle<float> (mid - 0.5f, row.getY() - 4.0f, 1.0f, row.getHeight() + 8.0f));
            caption (g, area, "VALUE NOW", juce::Justification::topLeft, words());
            caption (g, area, juce::String (juce::roundToInt (100.0f * v)) + " %", juce::Justification::topRight, look.trace);
            return;
        }
        const float peak = peakOf (s, id);
        meter (g, juce::Rectangle<float> (plot.getX(), plot.getCentreY() - 8.0f, plot.getWidth(), 10.0f), peak, look.trace);
        caption (g, area, "OUTPUT NOW", juce::Justification::topLeft, words());
        caption (g, area, dbText (peak), juce::Justification::topRight, look.trace);
    }

    /** The module's own live picture (its tile's, at size) with what the glass has room to say. */
    void paintLive (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto* n = s.document().findNode (id);
        if (n == nullptr)
            return;
        const auto& type = n->type;
        const bool steps = type == "event.arp" || type == "event.stepseq" || type == "event.generative" || type == "event.clock";
        if (steps || type == "proc.morph" || type == "proc.morph.global")
        {
            paintModuleDisplay (g, area.reduced (10.0f, 24.0f), s, id, false, look, 2.2f);
            if (steps)
                caption (g, area, type == "event.arp" ? "THE NOTES IT PLAYS, IN ORDER" : "STEPS  THE LIT ONE PLAYS NOW", juce::Justification::topLeft, words());
            else
            {
                caption (g, area, "A", juce::Justification::topLeft, colour::identity (0).wave);
                caption (g, area, "     B   THE TWO SOUNDS, BAND BY BAND, NOW", juce::Justification::topLeft, colour::identity (1).wave);
                caption (g, area, "MORPH " + juce::String (juce::roundToInt (100.0f * valueOf (s, id, "position"))) + " %", juce::Justification::topRight, look.trace);
            }
            return;
        }
        if (type == "util.voicemix")
        {
            const auto plot = plotIn (area);
            const float peak = peakOf (s, id);
            meter (g, juce::Rectangle<float> (plot.getX(), plot.getCentreY() - 8.0f, plot.getWidth(), 10.0f), peak, look.trace);
            caption (g, area, juce::String (juce::roundToInt (telemetryOf (liveOf (s, id), 0))) + " VOICES SOUNDING", juce::Justification::topLeft, words());
            caption (g, area, dbText (peak), juce::Justification::topRight, look.trace);
            return;
        }
        paintMeter (g, area, s, id, look);
    }

    /** PHASER: where its notches are now (the processor's own, on 20 Hz .. 20 kHz) and CENTRE. */
    void paintPhaser (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 3, 2);
        const auto xOf = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f); };
        g.setColour (words().withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (xOf (valueOf (s, id, "centre")), plot.getY(), 1.0f, plot.getHeight()));
        const auto* live = liveOf (s, id);
        int notches = 0;
        for (int i = 0; i < 6; ++i)
            if (const float hz = telemetryOf (live, i); hz > 20.0f)
            {
                const float x = xOf (hz);
                g.setGradientFill (juce::ColourGradient (look.trace.withAlpha (0.0f), x - 9.0f, 0.0f, look.trace.withAlpha (0.0f), x + 9.0f, 0.0f, false));
                g.setColour (look.trace.withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (x - 5.0f, plot.getY(), 10.0f, plot.getHeight()));
                g.setColour (look.trace);
                g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getY(), 1.5f, plot.getHeight()));
                ++notches;
            }
        caption (g, area, notches > 0 ? "NOTCHES NOW" : "ITS NOTCHES SHOW WHILE IT RUNS", juce::Justification::topLeft, words());
        caption (g, area, "20 Hz", juce::Justification::bottomLeft, words().withAlpha (0.6f));
        caption (g, area, "20 kHz", juce::Justification::bottomRight, words().withAlpha (0.6f));
    }

    /** FLANGER: its delay now (the processor's own) against DELAY, 0 .. 12 ms. */
    void paintFlanger (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        grid (g, plot, 6, 2);
        const auto xOf = [plot] (float ms) { return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 12.0f, ms) / 12.0f; };
        const float set = valueOf (s, id, "delay"), now = telemetryOf (liveOf (s, id), 0);
        g.setColour (words().withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (xOf (set), plot.getY(), 1.0f, plot.getHeight()));
        if (now > 0.0f)
        {
            g.setColour (look.trace.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (std::min (xOf (set), xOf (now)), plot.getCentreY() - 10.0f, std::abs (xOf (now) - xOf (set)), 20.0f));
            g.setColour (look.trace);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ xOf (now), plot.getCentreY() }));
        }
        caption (g, area, "DELAY NOW", juce::Justification::topLeft, words());
        caption (g, area, now > 0.0f ? juce::String (now, 2) + " ms" : juce::String (set, 2) + " ms", juce::Justification::topRight, look.trace);
        caption (g, area, "0", juce::Justification::bottomLeft, words().withAlpha (0.6f));
        caption (g, area, "12 ms", juce::Justification::bottomRight, words().withAlpha (0.6f));
    }

    /** STEREO SPREAD: what it does to the picture now: correlation, and side against mid. */
    void paintSpread (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        const auto* live = liveOf (s, id);
        const float corr = live != nullptr && live->telemetryCount.load() > 0 ? juce::jlimit (-1.0f, 1.0f, telemetryOf (live, 0)) : 1.0f;
        const auto row = juce::Rectangle<float> (plot.getX(), plot.getY() + 0.25f * plot.getHeight() - 5.0f, plot.getWidth(), 10.0f);
        g.setColour (colour::wellGrid);
        g.fillRoundedRectangle (row, 2.5f);
        g.setColour (corr < 0.0f ? juce::Colour (0xffe2684a) : look.trace);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ row.getCentreX() + 0.5f * row.getWidth() * corr, row.getCentreY() }));
        meter (g, juce::Rectangle<float> (plot.getX(), plot.getY() + 0.68f * plot.getHeight() - 5.0f, plot.getWidth(), 10.0f), telemetryOf (live, 1), look.trace);
        caption (g, area, "CORRELATION  -1 .. +1", juce::Justification::topLeft, words());
        caption (g, area, juce::String (corr, 2), juce::Justification::topRight, look.trace);
        caption (g, area, "SIDE LEVEL", juce::Justification::centredLeft, words());
    }

    /** RETRO: what the machine is doing to the signal now (the processor's own figures). */
    void paintRetro (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        const auto* live = liveOf (s, id);
        const bool running = live != nullptr && live->telemetryCount.load() >= 4;
        const float bits = running ? telemetryOf (live, 0) : 16.0f, rate = running ? telemetryOf (live, 1) : 48.0f, wow = running ? telemetryOf (live, 2) : 0.0f;
        struct Row
        {
            const char* name;
            float fraction;
            juce::String value;
        };
        const Row rows[] = { { "BITS", juce::jlimit (0.0f, 1.0f, bits / 16.0f), juce::String (bits, 1) },
                             { "RATE", juce::jlimit (0.0f, 1.0f, rate / 48.0f), juce::String (rate, 1) + " kHz" },
                             { "WOW", juce::jlimit (0.0f, 1.0f, std::abs (wow) / 50.0f), juce::String (wow, 1) + " ct" } };
        const float h = plot.getHeight() / 3.0f;
        g.setFont (fonts::make (9.5f, Weight::medium, 0.1f));
        for (int i = 0; i < 3; ++i)
        {
            const auto line = juce::Rectangle<float> (plot.getX(), plot.getY() + h * static_cast<float> (i), plot.getWidth(), h);
            const auto bar = juce::Rectangle<float> (line.getX() + 52.0f, line.getCentreY() - 3.0f, line.getWidth() - 132.0f, 6.0f);
            g.setColour (words());
            g.drawText (rows[i].name, line.withWidth (50.0f), juce::Justification::centredLeft, false);
            g.setColour (colour::wellGrid);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (look.trace);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * rows[i].fraction), 2.0f);
            g.drawText (rows[i].value, line.withTrimmedLeft (line.getWidth() - 74.0f), juce::Justification::centredRight, false);
        }
        caption (g, area, "THE MACHINE NOW", juce::Justification::topLeft, words());
    }

    /** MIXER: its four channels as set (level; muted or not soloed: dark), the master, and
        what leaves it now. */
    void paintMixer (juce::Graphics& g, juce::Rectangle<float> area, Session& s, const std::string& id, const ModuleLook& look)
    {
        const auto plot = plotIn (area);
        bool anySolo = false;
        for (int i = 1; i <= 4; ++i)
            anySolo = anySolo || valueOf (s, id, ("solo" + std::to_string (i)).c_str()) >= 0.5f;
        const float w = plot.getWidth() / 6.5f;
        g.setFont (fonts::make (9.0f, Weight::medium, 0.08f));
        for (int i = 0; i < 5; ++i)
        {
            const bool master = i == 4;
            const auto n = std::to_string (i + 1);
            const float db = master ? valueOf (s, id, "master") : valueOf (s, id, ("level" + n).c_str());
            const bool heard = master || (valueOf (s, id, ("mute" + n).c_str()) < 0.5f && (! anySolo || valueOf (s, id, ("solo" + n).c_str()) >= 0.5f));
            const auto lane = juce::Rectangle<float> (plot.getX() + w * (static_cast<float> (i) + (master ? 0.5f : 0.0f)) + 6.0f, plot.getY(), w - 12.0f, plot.getHeight() - 14.0f);
            g.setColour (colour::wellGrid);
            g.fillRoundedRectangle (lane.withSizeKeepingCentre (8.0f, lane.getHeight()), 3.0f);
            const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            g.setColour (heard ? look.trace : words().withAlpha (0.35f));
            g.fillRoundedRectangle (lane.withSizeKeepingCentre (8.0f, lane.getHeight()).withTop (lane.getBottom() - lane.getHeight() * f), 3.0f);
            g.setColour (words());
            g.drawText (master ? juce::String ("MASTER") : juce::String (i + 1), juce::Rectangle<float> (lane.getX() - 14.0f, lane.getBottom() + 2.0f, lane.getWidth() + 28.0f, 12.0f), juce::Justification::centred, false);
        }
        const float peak = peakOf (s, id);
        const auto out = juce::Rectangle<float> (plot.getRight() - 12.0f, plot.getY(), 8.0f, plot.getHeight() - 14.0f);
        g.setColour (colour::wellGrid);
        g.fillRoundedRectangle (out, 3.0f);
        g.setColour (peak > 0.98f ? juce::Colour (0xffe2684a) : look.trace);
        g.fillRoundedRectangle (out.withTop (out.getBottom() - out.getHeight() * juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -100.0f) + 48.0f) / 48.0f)), 3.0f);
        caption (g, area, "CHANNELS AS SET", juce::Justification::topLeft, words());
        caption (g, area, "OUT " + dbText (peak), juce::Justification::topRight, look.trace);
    }

    //==============================================================================================
    /** What each module's rack shows (the rest: the module's own order, its live display). */
    ModuleRackSpec specFor (const std::string& type)
    {
        ModuleRackSpec spec;
        spec.graph = paintLive;
        if (type.rfind ("osc.", 0) == 0 || type.rfind ("src.", 0) == 0)
        {
            spec.graph = paintSource;
            spec.subtitle = "SOUND SOURCE";
            spec.share = 0.6f;
        }
        else if (type == "proc.filter")
        {
            spec.graph = paintFilter;
            spec.subtitle = "VOICE FILTER";
            spec.first = { "cutoff", "resonance", "drive", "envAmount", "keytrack", "comp" };
        }
        else if (type == "fx.filter")
            spec.graph = paintFxFilter;
        else if (type == "fx.phaser")
            spec.graph = paintPhaser;
        else if (type == "fx.flanger")
            spec.graph = paintFlanger;
        else if (type == "fx.spread")
            spec.graph = paintSpread;
        else if (type == "fx.retro")
            spec.graph = paintRetro;
        else if (type == "util.mixer")
        {
            spec.graph = paintMixer;
            spec.subtitle = "MIXER";
            spec.pages = { { "LEVELS", { "level1", "level2", "level3", "level4", "master" } },
                           { "PAN", { "pan1", "pan2", "pan3", "pan4" } },
                           { "MUTE / SOLO", { "mute1", "mute2", "mute3", "mute4", "solo1", "solo2", "solo3", "solo4" } } };
        }
        else if (type == "event.stepseq")
        {
            // its sixteen steps are a picture to draw on, not forty-eight knobs
            spec.subtitle = "NOTES";
            spec.share = 0.5f;
            spec.skip = [] (const ParamDesc& p) {
                return p.id.size() <= 3 && (p.id[0] == 'p' || p.id[0] == 'g' || p.id[0] == 'v') && std::isdigit (static_cast<unsigned char> (p.id[1]));
            };
            spec.overlay = [] (Session& s, const std::string& id) -> std::unique_ptr<juce::Component> {
                auto grid = std::make_unique<StepGrid> (s, id);
                grid->setTooltip ("Drag: pitch.  Shift + drag: velocity.  Lower row: gates.");
                return grid;
            };
        }
        else if (type == "proc.drive")
            spec.graph = paintDrive;
        else if (type == "fx.compressor")
            spec.graph = paintCompressor;
        else if (type == "ctl.env" || type == "proc.vca")
        {
            spec.graph = paintEnvelope;
            spec.subtitle = type == "proc.vca" ? "AMP ENVELOPE" : "ENVELOPE";
            spec.first = { "attack", "decay", "sustain", "release" };
        }
        else if (type == "ctl.lfo")
        {
            spec.graph = paintLfo;
            spec.subtitle = "MODULATOR";
        }
        else if (type == "spatial.satellite")
        {
            spec.graph = paintSatellite;
            spec.subtitle = "SPATIAL MOTION";
        }
        if (spec.subtitle.isEmpty() && type.rfind ("event.", 0) == 0)
            spec.subtitle = "NOTES";
        if (spec.subtitle.isEmpty() && type.rfind ("ctl.", 0) == 0)
            spec.subtitle = "MODULATOR";
        if (spec.subtitle.isEmpty() && type.rfind ("util.", 0) == 0)
            spec.subtitle = "UTILITY";
        return spec;
    }

    /** The faceplate: Analog's device identity where the module wears one (its tile's enamel
        and motif); else the family's ceramic chassis with a rail in the module's own colour. */
    Faceplate plateFor (const ModuleLook& look, const juce::String& subtitle, const juce::String& where)
    {
        if (look.id != nullptr)
        {
            auto f = an::faceplateFromIdentity (*look.id);
            f.subtitle = (subtitle.isNotEmpty() ? subtitle : f.subtitle) + "   " + where;
            return f;
        }
        Faceplate f;
        f.fullBleed = true;
        const auto rail = look.badge >= 0 ? colour::identity (look.badge).badgeBottom : (look.face == ModuleLook::Face::key ? colour::arp.darker (0.25f) : colour::accent.darker (0.15f));
        f.enamel = rail;
        f.stripes = { rail, rail.brighter (0.35f), rail.brighter (0.8f) };
        f.glow = look.trace;
        f.glassTop = colour::wellA;
        f.glassBottom = colour::wellB;
        f.subtitle = subtitle + (subtitle.isNotEmpty() ? "   " : "") + where;
        return f;
    }

    //==============================================================================================
    class Glass final : public GraphFrame
    {
    public:
        Glass (Session& s, std::string node, ModuleLook l, GraphPainter p) : session (s), id (std::move (node)), look (std::move (l)), painter (std::move (p)) {}
        void refresh() override { repaint(); }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            if (painter && session.document().findNode (id) != nullptr)
                painter (g, area, session, id, look);
        }

    private:
        Session& session;
        std::string id;
        ModuleLook look;
        GraphPainter painter;
    };

    /** A key on the rail, in the faceplate's ink (a recording's name, a plugin's state): a click
        opens its actions. */
    class RailKey final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<juce::String()> text;
        std::function<void (juce::Component&)> onClick;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
            const auto ink = juce::Colour (0xfff3eadb);
            g.setColour (ink.withAlpha (hover ? 0.20f : 0.10f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (ink.withAlpha (0.38f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            g.setColour (ink.withAlpha (0.95f));
            g.setFont (fonts::make (10.5f, Weight::medium, 0.08f));
            g.drawFittedText (text ? text() : juce::String(), getLocalBounds().reduced (9, 0).withTrimmedRight (10), juce::Justification::centredLeft, 1, 0.8f);
            icon::draw (g, icon::Kind::down, juce::Rectangle<float> (static_cast<float> (getWidth()) - 18.0f, 0.5f * static_cast<float> (getHeight()) - 6.0f, 12.0f, 12.0f), ink.withAlpha (0.8f), 1.3f);
        }
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick && getLocalBounds().contains (e.getPosition()))
                onClick (*this);
        }

    private:
        bool hover = false;
    };

    std::map<std::string, int>& shownPages()
    {
        static std::map<std::string, int> pages;   // (interface only: the page a type's rack last showed)
        return pages;
    }

    class ModuleRack final : public ParameterRack
    {
    public:
        ModuleRack (RackHost& h, Session& s, std::string node, ModuleRackSpec sp, ModuleLook l)
            : ParameterRack (h, "MODULE", l.arc, sp.share), session (s), id (std::move (node)), spec (std::move (sp)), look (std::move (l))
        {
            const auto* n = session.document().findNode (id);
            desc = session.registry().find (n->type);
            setHeading (juce::String (session.document().displayName (session.registry(), *n)).toUpperCase());
            setHeaderSwitch ("m.on", "ON", "The module's light: on, or bypassed (a short cross-fade; held notes carry on)");
            mods = modulationsOf (session.document(), id);
            plan();
            if (pages.size() > 1)
            {
                juce::StringArray names;
                for (const auto& page : pages)
                    names.add (page.name);
                setPages (names, &shownPages()[n->type]);
            }
            setGraph (std::make_unique<Glass> (session, id, look, spec.graph));
            setFaceplate (plateFor (look, spec.subtitle, n->domain == Domain::voice ? "PER VOICE" : (n->domain == Domain::event ? "NOTE STREAM" : "GLOBAL")));
            addRailActions (*n);
            if (spec.overlay)
            {
                overlay = spec.overlay (session, id);
                if (overlay != nullptr)
                    addAndMakeVisible (*overlay);
            }
        }

        void resized() override
        {
            ParameterRack::resized();
            if (overlay != nullptr)
                overlay->setBounds (graphBounds().reduced (3));
        }

        void refresh() override
        {
            // (the module's settings decide which controls apply: as its tile)
            if (signature() != planned)
            {
                const auto before = pages.size();
                plan();
                if (pages.size() == before)
                    bump = (bump + 1) % 1000;
            }
            if (auto* key = railComponent())
                key->repaint();
            if (overlay != nullptr)
                overlay->repaint();
            ParameterRack::refresh();
        }

    protected:
        int buildKey() const override { return bump; }

        void build (int page) override
        {
            using P = rackui::Position;
            const auto* n = session.document().findNode (id);
            if (n == nullptr || desc == nullptr)
                return;
            // A sound source: what kind of sound it is, first (the same choice as on its card).
            if (desc->role == NodeRole::source && host.apvts().getParameter ("m.sourceType") != nullptr)
                selectorList ("m.sourceType", "TYPE", 132, "This source's type. Each type keeps its own settings, recording and cables.");
            if (selector != nullptr)
                choiceControl (*selector, true);
            if (page < 0 || page >= static_cast<int> (pages.size()))
                return;
            const auto& shown = pages[static_cast<std::size_t> (page)];
            for (const auto* p : shown.params)
            {
                if (p->curve == ParamCurve::choice)
                    choiceControl (*p, false);
                else if (p->curve == ParamCurve::toggle)
                    switchCell (rackId (p->id), juce::String (p->name), { P { "ON", "On", 1.0f }, P { "OFF", "Off", 0.0f } }, juce::String (p->name));
                else
                {
                    const double k = asPercent (*p) ? 100.0 : 1.0;
                    auto format = [p, k] (double v) {
                        const double native = v / k;
                        if (p->id == "root" && native < 0.0)
                            return juce::String ("AUTO");   // (the recording's own, as analysed)
                        if (p->unit.empty() && p->min == -1.0 && p->max == 1.0)
                            return std::abs (native) < 0.005 ? juce::String ("C") : juce::String (native < 0.0 ? "L " : "R ") + juce::String (juce::roundToInt (100.0 * std::abs (native)));
                        return juce::String (p->format (native));
                    };
                    if (p->curve == ParamCurve::integer)
                        steppedKnob (rackId (p->id), juce::String (p->name), format);
                    else
                        knob (rackId (p->id), juce::String (p->name), format);
                }
            }
            if (shown.modulation)
            {
                // What moves this module's controls: each cable's depth (-100 .. +100 %).
                group ("MODULATED BY");
                auto& doc = session.document();
                for (std::size_t i = 0; i < mods.size() && i < 8; ++i)
                    if (const auto* c = doc.findConnection (mods[i]))
                    {
                        // (named by what it moves: "CUTOFF"; the source is the cable on the board)
                        const auto* from = doc.findNode (c->from);
                        const auto source = (from != nullptr ? juce::String (doc.displayName (session.registry(), *from)) : juce::String ("?")).toUpperCase();
                        knob ("m.mod." + juce::String (static_cast<int> (i)), (source.substring (0, 7) + " > " + juce::String (c->toPort).toUpperCase()).substring (0, 16),
                              [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; });
                    }
            }
        }

    private:
        struct Page
        {
            juce::String name;
            std::vector<const ParamDesc*> params;
            bool modulation = false;
        };

        void choiceControl (const ParamDesc& p, bool inSelectorRow)
        {
            using P = rackui::Position;
            std::vector<P> positions;
            for (std::size_t i = 0; i < p.choices.size(); ++i)
                positions.push_back ({ juce::String (p.choices[i]), juce::String (p.choices[i]), static_cast<float> (i) });
            const auto idOf = rackId (p.id);
            const auto name = juce::String (p.name);
            if (inSelectorRow)
            {
                if (segmented (p, 4, 26))
                    selectorSegment (idOf, name, std::move (positions), juce::jlimit (120, 330, 30 * static_cast<int> (p.choices.size()) + 7 * letters (p)), name);
                else
                    selectorList (idOf, name, 170, name);
            }
            else if (segmented (p, 3, 16))
                segmentCell (idOf, name, std::move (positions), name);
            else
                listCell (idOf, name, name);
        }

        static int letters (const ParamDesc& p)
        {
            int n = 0;
            for (const auto& c : p.choices)
                n += static_cast<int> (c.size());
            return n;
        }
        static bool segmented (const ParamDesc& p, int most, int mostLetters) { return static_cast<int> (p.choices.size()) <= most && letters (p) <= mostLetters; }

        /** The settings that decide what is shown (every choice and switch). */
        int signature() const
        {
            const auto* n = session.document().findNode (id);
            if (n == nullptr || desc == nullptr)
                return -1;
            std::uint32_t h = 17;
            for (const auto& p : desc->params)
                if (p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle)
                    h = h * 31u + static_cast<std::uint32_t> (juce::roundToInt (session.document().paramValue (session.registry(), id, p.id)) + 1);
            return static_cast<int> (h & 0x7fffffff);
        }

        /** A cell's slot on the row (ParameterRack's: a knob or a list 104 px, a three-way
            switch 119, an on / off switch 95; from eight cells on the row is set closer). */
        rackui::Slot slotOf (const ParamDesc& p) const
        {
            if (p.curve == ParamCurve::toggle)
                return rackui::Slot::slim;
            return p.curve == ParamCurve::choice && segmented (p, 3, 16) ? rackui::Slot::wide : rackui::Slot::knob;
        }
        bool fits (const std::vector<const ParamDesc*>& cells, int extraKnobs = 0) const
        {
            // (the rack is laid out in reference pixels: its width is the board's, always)
            const int count = static_cast<int> (cells.size()) + extraKnobs;
            const bool dense = count >= rackui::denseFrom;
            const int room = juce::roundToInt (spec.share * static_cast<float> (1355 - 54)) - rackui::Cell::columnGap;
            int width = extraKnobs * rackui::slotWidth (rackui::Slot::knob, dense);
            for (const auto* p : cells)
                width += rackui::slotWidth (slotOf (*p), dense);
            return width + std::max (0, count - 1) * rackui::slotGap (dense) <= room;
        }

        /** Which parameters the rack shows, and on which page: its mode in the selector row,
            then what applies in that mode, a row at a time (evenly, when it takes several). */
        void plan()
        {
            planned = signature();
            pages.clear();
            selector = nullptr;
            const auto* n = session.document().findNode (id);
            if (n == nullptr || desc == nullptr)
                return;
            const auto applies = [&] (const ParamDesc& p) { return desc->paramShown (p, n->params) && ! (spec.skip && spec.skip (p)); };
            std::vector<const ParamDesc*> shown;
            for (const auto& wanted : spec.first)
                if (const int i = desc->paramIndex (wanted); i >= 0 && applies (desc->params[static_cast<std::size_t> (i)]))
                    shown.push_back (&desc->params[static_cast<std::size_t> (i)]);
            for (const auto& p : desc->params)
                if (applies (p) && std::find (shown.begin(), shown.end(), &p) == shown.end())
                    shown.push_back (&p);
            // the mode: the named choice, else the module's first quick choice
            for (const auto* p : shown)
                if (p->curve == ParamCurve::choice && (spec.selector.empty() ? p->quick : p->id == spec.selector))
                {
                    selector = p;
                    break;
                }
            if (selector != nullptr)
                shown.erase (std::remove (shown.begin(), shown.end(), selector), shown.end());

            const auto flow = [this] (const juce::String& firstName, std::vector<const ParamDesc*> cells) {
                // a row is filled before the next is begun (the first page is the module's face);
                // a last row is never left with one lonely control
                std::vector<std::vector<const ParamDesc*>> rows;
                for (std::size_t at = 0; at < cells.size();)
                {
                    std::size_t take = std::min<std::size_t> (cells.size() - at, 10);
                    while (take > 1 && ! fits ({ cells.begin() + static_cast<std::ptrdiff_t> (at), cells.begin() + static_cast<std::ptrdiff_t> (at + take) }))
                        --take;
                    rows.emplace_back (cells.begin() + static_cast<std::ptrdiff_t> (at), cells.begin() + static_cast<std::ptrdiff_t> (at + take));
                    at += take;
                }
                if (rows.size() > 1 && rows.back().size() == 1 && rows[rows.size() - 2].size() > 3)
                {
                    rows.back().insert (rows.back().begin(), rows[rows.size() - 2].back());
                    rows[rows.size() - 2].pop_back();
                }
                if (rows.empty())
                    rows.emplace_back();
                for (std::size_t index = 0; index < rows.size(); ++index)
                {
                    Page page;
                    page.name = index == 0 ? firstName : (rows.size() == 2 ? juce::String ("MORE") : "MORE " + juce::String (static_cast<int> (index)));
                    page.params = std::move (rows[index]);
                    pages.push_back (std::move (page));
                }
            };
            if (spec.pages.empty())
                flow ("MAIN", shown);
            else
            {
                std::vector<const ParamDesc*> rest = shown;
                for (const auto& [name, ids] : spec.pages)
                {
                    std::vector<const ParamDesc*> cells;
                    for (const auto& pid : ids)
                        for (const auto* p : shown)
                            if (p->id == pid)
                            {
                                cells.push_back (p);
                                rest.erase (std::remove (rest.begin(), rest.end(), p), rest.end());
                            }
                    if (! cells.empty())
                        flow (name, cells);
                }
                if (! rest.empty())
                    flow ("MORE", rest);
            }
            // its modulations: on the last page if they fit there, else on a page of their own
            if (! mods.empty())
            {
                const int count = static_cast<int> (std::min<std::size_t> (8, mods.size()));
                if (pages.size() == 1 && fits (pages.back().params, count + 1))
                    pages.back().modulation = true;
                else
                {
                    Page page;
                    page.name = "MODULATION";
                    page.modulation = true;
                    pages.push_back (std::move (page));
                }
            }
        }

        /** What a module has besides parameters: its recording, its hosted plugin (the rail). */
        void addRailActions (const NodeModel& n)
        {
            if (n.strings.count ("sample") > 0)
            {
                auto key = std::make_unique<RailKey>();
                key->text = [this] {
                    const auto* node = session.document().findNode (id);
                    const auto it = node != nullptr ? node->strings.find ("sample") : std::map<std::string, std::string>::const_iterator();
                    const bool has = node != nullptr && it != node->strings.end() && ! it->second.empty();
                    return has ? session.resolveKey (it->second).getFileNameWithoutExtension().toUpperCase() : juce::String ("CHOOSE A RECORDING");
                };
                key->setTooltip ("This source's recording: click to choose another (or drop a file on its tile)");
                key->onClick = [this] (juce::Component&) { chooseRecording(); };
                setRailComponent (std::move (key), 230);
            }
            else if (n.type == "src.external")
            {
                auto key = std::make_unique<RailKey>();
                key->text = [this] { return session.externals().status (id).toUpperCase(); };
                key->setTooltip ("The hosted instrument: choose it, or open its own window");
                key->onClick = [this] (juce::Component& target) {
                    juce::PopupMenu menu;
                    menu.addItem ("Choose plugin...", [this] { choosePlugin(); });
                    menu.addItem ("Open its window", [this] {
                        if (! session.externals().openEditor (id))
                            session.message ("no plugin window: " + session.externals().status (id), false, true);
                    });
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
                };
                setRailComponent (std::move (key), 260);
            }
        }

        void chooseRecording()
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Choose a recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [&s = session, node = id, chooser] (const juce::FileChooser& fc) {
                                      if (fc.getResult() != juce::File())
                                          s.setNodeString (node, "sample", s.keyFor (fc.getResult()));
                                  });
        }

        void choosePlugin()
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Choose an instrument plugin", juce::File(), "*.vst3;*.component");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                  [&s = session, node = id, chooser] (const juce::FileChooser& fc) {
                                      if (fc.getResult() != juce::File())
                                          s.setNodeString (node, "plugin", fc.getResult().getFullPathName().toStdString());
                                  });
        }

        Session& session;
        std::string id;
        ModuleRackSpec spec;
        ModuleLook look;
        const NodeDesc* desc = nullptr;
        const ParamDesc* selector = nullptr;
        std::vector<Page> pages;
        std::vector<std::string> mods;
        std::unique_ptr<juce::Component> overlay;
        int planned = 0, bump = 0;
    };
}

std::unique_ptr<NativeRack> createModuleRack (Session& s, const std::string& id)
{
    const auto* n = s.document().findNode (id);
    const auto* d = n != nullptr ? s.registry().find (n->type) : nullptr;
    if (d == nullptr)
        return nullptr;
    int ordinal = -1, sources = 0;
    for (const auto& other : s.document().nodes)
        if (const auto* od = s.registry().find (other.type); od != nullptr && od->role == NodeRole::source)
        {
            if (other.id == id)
                ordinal = sources;
            ++sources;
        }
    const auto look = lookFor (n->type, ordinal);
    std::vector<RackParam> binding;
    auto layout = layoutFor (*d, binding);
    auto rack = std::make_unique<NativeRack>();
    auto* self = rack.get();
    // ON is the module's light (its bypass), not a parameter of its DSP.
    RackParam on { "m.on", {}, 1.0, 0.0, {}, {}, {}, {} };
    on.read = [&s, id] {
        const auto* node = s.document().findNode (id);
        return node != nullptr && node->enabled ? 1.0 : 0.0;
    };
    on.write = [&s, id] (double v) { s.setEnabled (id, v >= 0.5); };
    binding.push_back (std::move (on));
    // A sound source's TYPE (not a parameter: it changes which module this is). Applied after
    // the click has returned, because the rack is built again for the new type.
    if (d->role == NodeRole::source && ! edit::sourceTypeName (n->type).empty())
    {
        juce::StringArray names;
        for (const auto& t : edit::sourceTypes())
            names.add (t.name);
        const auto indexOf = [] (const std::string& type) {
            const auto& types = edit::sourceTypes();
            for (std::size_t i = 0; i < types.size(); ++i)
                if (type == types[i].type)
                    return static_cast<int> (i);
            return 0;
        };
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "m.sourceType", 1 }, "Source type", names, indexOf (n->type)));
        RackParam kind { "m.sourceType", {}, 1.0, 0.0, {}, {}, {}, {} };
        kind.read = [&s, id, indexOf] {
            const auto* node = s.document().findNode (id);
            return node != nullptr ? static_cast<double> (indexOf (node->type)) : 0.0;
        };
        kind.write = [&s, id] (double v) {
            const auto& types = edit::sourceTypes();
            const std::string type = types[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (types.size()) - 1, juce::roundToInt (v)))].type;
            juce::MessageManager::callAsync ([&s, id, type] {
                if (const auto* node = s.document().findNode (id); node != nullptr && node->type != type)
                    s.changeSourceType (id, type);
            });
        };
        binding.push_back (std::move (kind));
    }
    // Each cable that modulates one of its parameters: its depth.
    const auto mods = modulationsOf (s.document(), id);
    for (std::size_t i = 0; i < mods.size() && i < 8; ++i)
    {
        const auto pid = "m.mod." + juce::String (static_cast<int> (i));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, 1 }, "Modulation depth", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f));
        RackParam depth { pid, {}, 1.0, 0.0, {}, {}, {}, {} };
        depth.read = [&s, cable = mods[i]] {
            const auto* c = s.document().findConnection (cable);
            return c != nullptr ? 100.0 * c->depth : 0.0;
        };
        depth.write = [&s, cable = mods[i], self] (double v) {
            if (auto* c = s.document().findConnection (cable))
            {
                self->host->beginAction();
                c->depth = 0.01 * v;
                s.recompile();
            }
        };
        binding.push_back (std::move (depth));
    }
    rack->host = std::make_unique<RackHost> (s, id, std::move (layout), std::move (binding));
    rack->make = [host = rack->host.get(), &s, id, look, type = n->type] () -> std::unique_ptr<osp::plugin::rack::Module> {
        return std::make_unique<ModuleRack> (*host, s, id, specFor (type), look);
    };
    rack->shape = [&s, id] {
        std::string all;
        for (const auto& c : modulationsOf (s.document(), id))
            all += c + ";";
        return all;
    };
    rack->provenance = "parts";
    return rack;
}

} // namespace andor::modular::ui
