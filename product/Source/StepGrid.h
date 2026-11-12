#pragma once

#include "Session.h"
#include "Theme.h"

namespace andor::modular::ui
{

/** STEP SEQUENCER's 16 steps: pitch bars (drag), gate cells (click), velocity (Shift-drag). */
class StepGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepGrid (Session& s, std::string n) : session (s), node (std::move (n)) { setTitle ("Step editor"); }
    void paint (juce::Graphics& g) override
    {
        // The family's graphite display: sixteen steps, pitch as a bar, the gate row under it.
        const auto& doc = session.document();
        const auto* live = session.graph();
        int current = -1;
        if (live != nullptr)
            if (const auto it = live->nodeIndexById.find (node); it != live->nodeIndexById.end())
                current = static_cast<int> (live->nodes[static_cast<std::size_t> (it->second)].telemetry[0].load());
        const int length = static_cast<int> (doc.paramValue (session.registry(), node, "length"));
        draw::well (g, getLocalBounds().toFloat(), 7.0f, colour::wellA);
        const auto inner = getLocalBounds().toFloat().reduced (5.0f, 5.0f);
        const float w = inner.getWidth() / 16.0f, h = inner.getHeight() - 18.0f;
        const auto lit = colour::arpOnDark;
        for (int s = 0; s < 16; ++s)
        {
            const auto id = std::to_string (s + 1);
            const double pitch = doc.paramValue (session.registry(), node, "p" + id);
            const bool gate = doc.paramValue (session.registry(), node, "g" + id) >= 0.5;
            const double vel = doc.paramValue (session.registry(), node, "v" + id);
            const auto cell = juce::Rectangle<float> (inner.getX() + w * static_cast<float> (s) + 1.0f, inner.getY(), w - 2.0f, h);
            g.setColour (colour::wellGrid.withAlpha (s < length ? 0.75f : 0.25f));
            g.fillRoundedRectangle (cell, 1.5f);
            g.setColour (colour::wellRim.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getCentreY() - 0.5f, cell.getWidth(), 1.0f));
            const float y = cell.getCentreY() - static_cast<float> (pitch / 24.0) * h * 0.5f;
            g.setColour (s == current ? juce::Colours::white : (gate ? lit : lit.withAlpha (0.3f)));
            g.fillRoundedRectangle (cell.getX() + 1.5f, y - 2.0f, cell.getWidth() - 3.0f, 4.0f, 1.5f);
            // the gate (its velocity as its light)
            g.setColour (gate ? lit.withAlpha (static_cast<float> (0.35 + 0.65 * vel)) : colour::wellGrid);
            g.fillRoundedRectangle (cell.getX() + 1.5f, inner.getY() + h + 5.0f, cell.getWidth() - 3.0f, 10.0f, 2.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        session.beginGesture ("steps");
        edit (e, true);
    }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }

private:
    void edit (const juce::MouseEvent& e, bool press)
    {
        const auto inner = getLocalBounds().toFloat().reduced (5.0f, 5.0f);
        const int s = juce::jlimit (0, 15, static_cast<int> ((e.position.x - inner.getX()) / (inner.getWidth() / 16.0f)));
        const auto id = std::to_string (s + 1);
        const float h = inner.getHeight() - 18.0f;
        if (e.position.y > inner.getY() + h)
        {
            if (press)
                session.setParam (node, "g" + id, session.document().paramValue (session.registry(), node, "g" + id) >= 0.5 ? 0.0 : 1.0);
        }
        else if (e.mods.isShiftDown())
            session.setParam (node, "v" + id, juce::jlimit (0.0, 1.0, 1.0 - static_cast<double> (e.position.y / h)));
        else
            session.setParam (node, "p" + id, std::round ((0.5 - static_cast<double> (e.position.y / h)) * 48.0));
        repaint();
    }
    Session& session;
    std::string node;
};

} // namespace andor::modular::ui
