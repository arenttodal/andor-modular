#include "DetailPanel.h"

#include "Frame.h"

namespace andor::modular::ui
{

namespace
{
    /** STEP SEQUENCER's 16 steps: pitch bars (drag), gate cells (click), velocity (Shift-drag). */
    class StepGrid final : public juce::Component
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

    /** An incoming modulation: its source, a depth slider, remove. */
    class ModRow final : public juce::Component
    {
    public:
        ModRow (Session& s, std::string connection) : session (s), id (std::move (connection))
        {
            depth.setRange (-1.0, 1.0);
            depth.setDoubleClickReturnValue (true, 0.0);
            if (const auto* c = session.document().findConnection (id))
                depth.setValue (c->depth, juce::dontSendNotification);
            depth.onDragStart = [this] { session.beginGesture ("modulation depth"); };
            depth.onValueChange = [this] {
                if (auto* c = session.document().findConnection (id))
                {
                    c->depth = depth.getValue();
                    session.recompile();
                }
            };
            depth.setTitle ("Modulation depth");
            addAndMakeVisible (depth);
            remove.setIcon (icon::Kind::close);
            remove.setTooltip ("Remove this modulation");
            remove.onClick = [this] {
                session.perform ("remove modulation", [this] (Document& d) {
                    d.disconnect (id);
                    edit::Result r;
                    r.ok = true;
                    r.message = "modulation removed";
                    return r;
                });
            };
            addAndMakeVisible (remove);
        }
        void paint (juce::Graphics& g) override
        {
            const auto* c = session.document().findConnection (id);
            if (c == nullptr)
                return;
            const auto* from = session.document().findNode (c->from);
            g.setColour (board::ink);
            g.setFont (fonts::make (11.5f, Weight::medium, 0.04f));
            g.drawText ((from != nullptr ? juce::String (session.document().displayName (session.registry(), *from)) : juce::String()).toUpperCase() + "   >   "
                            + juce::String (c->toPort).toUpperCase(),
                        getLocalBounds().removeFromTop (16).withTrimmedRight (50), juce::Justification::centredLeft, true);
            g.setColour (board::soft);
            g.drawText (juce::String (juce::roundToInt (depth.getValue() * 100.0)) + " %", getLocalBounds().removeFromTop (16).withTrimmedRight (26), juce::Justification::centredRight);
        }
        void resized() override
        {
            auto r = getLocalBounds().withTrimmedTop (16);
            remove.setBounds (r.removeFromRight (28));
            depth.setBounds (r);
        }

    private:
        Session& session;
        std::string id;
        RailFader depth;
        InkButton remove { juce::String() };
    };
}

//==============================================================================
class DetailPanel::Content final : public juce::Component
{
public:
    Content (Session& s, const std::string& id) : session (s), target (id)
    {
        const auto& doc = session.document();
        if (const auto* g = doc.findGroup (id))
        {
            title = juce::String (g->name);
            subtitle = "GROUP   " + juce::String (static_cast<int> (edit::groupMembers (doc, id).size())) + " modules, flattened for the budgets";
            help = "A group plays as its modules (each still counts against the patch's limits). Its controls appear on its card and can be mapped to the "
                   "patch's macros. Right-click its card to save it as a module (.andormodule).";
            for (std::size_t c = 0; c < g->controls.size(); ++c)
            {
                auto name = std::make_unique<juce::Label> ("c" + std::to_string (c), g->controls[c].name);
                name->setEditable (true);
                name->onTextChange = [this, c, n = name.get()] {
                    session.perform ("rename control", [&] (Document& d) {
                        edit::Result r;
                        if (auto* gm = d.findGroup (target); gm != nullptr && c < gm->controls.size())
                        {
                            gm->controls[c].name = n->getText().toUpperCase().toStdString();
                            r.ok = true;
                        }
                        return r;
                    });
                };
                addAndMakeVisible (*name);
                labels.push_back (std::move (name));
            }
            auto exportButton = std::make_unique<juce::TextButton> ("SAVE AS MODULE...");
            exportButton->onClick = [this] {
                auto chooser = std::make_shared<juce::FileChooser> ("Save module", Session::userDirectory().getChildFile ("Modules"), "*.andormodule");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
                    auto f = fc.getResult();
                    if (f == juce::File())
                        return;
                    f = f.withFileExtension ("andormodule");
                    f.getParentDirectory().createDirectory();
                    if (f.replaceWithText (juce::String (edit::exportModule (session.document(), target))))
                        session.message ("saved module " + f.getFileName());
                });
            };
            addAndMakeVisible (*exportButton);
            buttons.push_back (std::move (exportButton));
            layout();
            return;
        }
        const auto* n = doc.findNode (id);
        const auto* d = n != nullptr ? session.registry().find (n->type) : nullptr;
        if (d == nullptr)
        {
            title = n != nullptr ? juce::String ("MISSING ") + n->type : juce::String();
            help = "This module is not part of this version. It is kept, silent, with its settings, so a newer version can play it again.";
            layout();
            return;
        }
        title = juce::String (doc.displayName (session.registry(), *n));
        subtitle = juce::String (d->name) + "  " + (n->domain == Domain::voice ? "PER VOICE" : (n->domain == Domain::event ? "NOTES" : "GLOBAL"));
        help = juce::String (d->help.empty() ? d->description : d->help);
        // Recordings and plugins.
        if (n->strings.count ("sample") > 0)
        {
            auto b = std::make_unique<juce::TextButton> (n->strings.at ("sample").empty() ? "CHOOSE A RECORDING..." : "REPLACE / RELINK...");
            b->onClick = [this] {
                auto chooser = std::make_shared<juce::FileChooser> ("Choose a recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
                    if (fc.getResult() != juce::File())
                        session.setNodeString (target, "sample", session.keyFor (fc.getResult()));
                });
            };
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }
        if (n->type == "src.external")
        {
            auto choose = std::make_unique<juce::TextButton> ("CHOOSE PLUGIN...");
            choose->onClick = [this] {
                auto chooser = std::make_shared<juce::FileChooser> ("Choose an instrument plugin", juce::File(), "*.vst3;*.component");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                      [this, chooser] (const juce::FileChooser& fc) {
                                          if (fc.getResult() != juce::File())
                                              session.setNodeString (target, "plugin", fc.getResult().getFullPathName().toStdString());
                                      });
            };
            addAndMakeVisible (*choose);
            buttons.push_back (std::move (choose));
            auto open = std::make_unique<juce::TextButton> ("OPEN ITS WINDOW");
            open->onClick = [this] {
                if (! session.externals().openEditor (target))
                    session.message ("no plugin window: " + session.externals().status (target), false, true);
            };
            addAndMakeVisible (*open);
            buttons.push_back (std::move (open));
        }
        if (n->type == "event.stepseq")
        {
            steps = std::make_unique<StepGrid> (session, target);
            addAndMakeVisible (*steps);
        }
        for (const auto& p : d->params)
        {
            if (n->type == "event.stepseq" && (p.id.size() <= 3 && (p.id[0] == 'p' || p.id[0] == 'g' || p.id[0] == 'v') && std::isdigit (static_cast<unsigned char> (p.id[1]))))
                continue;   // in the step editor
            if (! d->paramShown (p, n->params))
                continue;   // does not apply in the current mode
            std::unique_ptr<juce::Component> c;
            if (p.curve == ParamCurve::choice)
                c = std::make_unique<ParamChoice> (session, target, p.id);
            else if (p.curve == ParamCurve::toggle)
                c = std::make_unique<ParamToggle> (session, target, p.id, false);
            else
            {
                auto knob = std::make_unique<ParamKnob> (session, target, p.id, ParamKnob::Style::detail);
                knob->setInks (board::soft, board::ink, colour::accent);
                c = std::move (knob);
            }
            addAndMakeVisible (*c);
            if (p.curve == ParamCurve::choice)
                switchNames.push_back (juce::String (p.name));
            else if (p.curve == ParamCurve::toggle)
                switchNames.push_back ({});
            (p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle ? switches : knobs).push_back (std::move (c));
        }
        for (const auto& c : doc.connections)
            if (c.toParam && c.to == target)
            {
                auto row = std::make_unique<ModRow> (session, c.id);
                addAndMakeVisible (*row);
                mods.push_back (std::move (row));
            }
        layout();
    }

    void layout()
    {
        const int w = static_cast<int> (layout::detailWidth) - 14, x0 = 16, inner = w - 2 * x0;
        const auto helpFont = type::micro (12.5f);
        int y = 66;
        juce::GlyphArrangement lines;
        lines.addFittedText (helpFont, help, 0.0f, 0.0f, static_cast<float> (inner), 400.0f, juce::Justification::topLeft, 12, 1.0f);
        helpHeight = help.isEmpty() ? 0 : juce::roundToInt (lines.getBoundingBox (0, -1, true).getHeight()) + 6;
        y += helpHeight + 10;
        for (auto& b : buttons)
        {
            b->setBounds (x0, y, inner, 32);
            y += 38;
        }
        for (auto& l : labels)
        {
            l->setBounds (x0, y, inner, 26);
            y += 30;
        }
        auto section = [&] (const juce::String& name) {
            y += 8;
            sectionY.push_back ({ y, name });
            y += 26;
        };
        if (! switches.empty())
        {
            section ("MODES");
            int col = 0;
            const int cw = (inner - 10) / 2;
            for (std::size_t i = 0; i < switches.size(); ++i)
            {
                // a choice gets its name above it; a toggle is a key that carries its own
                const bool named = i < switchNames.size() && switchNames[i].isNotEmpty();
                const int x = x0 + col * (cw + 10);
                if (named)
                    switchLabels.push_back ({ juce::Rectangle<int> (x + 2, y, cw, 14), switchNames[i] });
                switches[i]->setBounds (x, y + 16, cw, 30);
                if (++col == 2)
                {
                    col = 0;
                    y += 54;
                }
            }
            if (col != 0)
                y += 54;
        }
        if (! knobs.empty())
        {
            section ("CONTROLS");
            int col = 0;
            const int cw = inner / 4;
            for (auto& c : knobs)
            {
                c->setBounds (x0 + col * cw, y, cw, 90);
                if (++col == 4)
                {
                    col = 0;
                    y += 98;
                }
            }
            if (col != 0)
                y += 98;
        }
        if (steps != nullptr)
        {
            section ("STEPS");
            stepsHint = y - 22;
            steps->setBounds (x0, y, inner, 124);
            y += 132;
        }
        if (! mods.empty())
        {
            section ("MODULATED BY");
            for (auto& m : mods)
            {
                m->setBounds (x0, y, inner, 44);
                y += 48;
            }
        }
        setSize (w, y + 18);
    }

    void paint (juce::Graphics& g) override
    {
        // Ceramic keys (the toggles) are drawn by the surface they sit on.
        skin::drawKeys (g, *this);
        const int x0 = 16, inner = getWidth() - 2 * x0;
        g.setColour (board::ink);
        g.setFont (fonts::make (19.0f, Weight::bold, 0.05f));
        g.drawFittedText (title.toUpperCase(), x0, 12, inner - 30, 26, juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (board::soft);
        g.setFont (fonts::make (10.5f, Weight::medium, 0.12f));
        g.drawText (subtitle.toUpperCase(), x0, 38, inner, 16, juce::Justification::centredLeft, true);
        g.setColour (board::soft);
        g.setFont (type::micro (12.5f));
        g.drawFittedText (help, x0, 64, inner, helpHeight, juce::Justification::topLeft, 12, 1.0f);
        g.setFont (fonts::make (10.5f, Weight::medium, 0.06f));
        g.setColour (board::soft);
        for (const auto& [area, name] : switchLabels)
            g.drawText (name.toUpperCase(), area, juce::Justification::centredLeft, true);
        for (const auto& [y, name] : sectionY)
        {
            g.setColour (board::rule);
            g.fillRect (x0, y, inner, 1);
            g.setColour (board::weak.darker (0.25f));
            g.setFont (fonts::make (10.5f, Weight::medium, 0.14f));
            g.drawText (name, x0, y + 6, inner, 14, juce::Justification::centredLeft, false);
        }
        if (steps != nullptr)
        {
            g.setColour (board::weak);
            g.setFont (type::micro (10.5f));
            g.drawText ("drag pitch   Shift: velocity   lower row: gates", x0 + 56, stepsHint + 2, inner - 56, 14, juce::Justification::centredRight, false);
        }
    }

    void refresh()
    {
        for (auto& c : knobs)
            refreshControl (c.get());
        for (auto& c : switches)
            refreshControl (c.get());
        if (steps != nullptr)
            steps->repaint();
    }

private:
    Session& session;
    std::string target;
    juce::String title, subtitle, help;
    int helpHeight = 40, stepsHint = 0;
    std::vector<std::unique_ptr<juce::Component>> knobs, switches, buttons, labels, mods;
    std::unique_ptr<juce::Component> steps;
    std::vector<std::pair<int, juce::String>> sectionY;
    std::vector<juce::String> switchNames;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> switchLabels;
};

//==============================================================================
DetailPanel::DetailPanel (Session& s) : session (s)
{
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (7);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, board::column);
    addAndMakeVisible (viewport);
    close.setIcon (icon::Kind::close);
    close.setTooltip ("Close (Esc)");
    close.onClick = [this] {
        show ({});
        if (onClose)
            onClose();
    };
    addAndMakeVisible (close);
    setTitle ("Detail");
}

DetailPanel::~DetailPanel() { viewport.setViewedComponent (nullptr, false); }

void DetailPanel::show (const std::string& id)
{
    current = id;
    rebuild();
}

void DetailPanel::rebuild()
{
    const int scroll = viewport.getViewPositionY();
    viewport.setViewedComponent (nullptr, false);
    content.reset();
    if (! current.empty() && (session.document().findNode (current) != nullptr || session.document().findGroup (current) != nullptr))
    {
        content = std::make_unique<Content> (session, current);
        viewport.setViewedComponent (content.get(), false);
        viewport.setViewPosition (0, scroll);
    }
    else
        current.clear();
    repaint();
}

void DetailPanel::refreshValues()
{
    if (content != nullptr)
        content->refresh();
}

void DetailPanel::paint (juce::Graphics& g)
{
    // A column of the board (its face shows through): one rule on its left.
    g.setColour (board::rule);
    g.fillRect (0, 10, 1, getHeight() - 20);
    if (content == nullptr)
    {
        g.setColour (board::weak);
        g.setFont (type::micro (13.0f));
        g.drawFittedText ("Select a module to see every control.\nDouble-click a module, or press Return.", getLocalBounds().reduced (24), juce::Justification::centred, 4);
    }
}

void DetailPanel::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (2).withTrimmedBottom (6));
    close.setBounds (getWidth() - 44, 10, 32, 30);
}

} // namespace andor::modular::ui
