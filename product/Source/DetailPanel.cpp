#include "DetailPanel.h"

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
            const auto& doc = session.document();
            const auto* live = session.graph();
            int current = -1;
            if (live != nullptr)
                if (const auto it = live->nodeIndexById.find (node); it != live->nodeIndexById.end())
                    current = static_cast<int> (live->nodes[static_cast<std::size_t> (it->second)].telemetry[0].load());
            const int length = static_cast<int> (doc.paramValue (session.registry(), node, "length"));
            const float w = static_cast<float> (getWidth()) / 16.0f, h = static_cast<float> (getHeight()) - 20.0f;
            for (int s = 0; s < 16; ++s)
            {
                const auto id = std::to_string (s + 1);
                const double pitch = doc.paramValue (session.registry(), node, "p" + id);
                const bool gate = doc.paramValue (session.registry(), node, "g" + id) >= 0.5;
                const double vel = doc.paramValue (session.registry(), node, "v" + id);
                const auto cell = juce::Rectangle<float> (w * static_cast<float> (s) + 1.0f, 0.0f, w - 2.0f, h);
                g.setColour (s < length ? colour::peatDeep : colour::peat);
                g.fillRect (cell);
                const float y = cell.getCentreY() - static_cast<float> (pitch / 24.0) * h * 0.5f;
                g.setColour (s == current ? colour::ember : (gate ? colour::spore : colour::sporeDim));
                g.fillRect (cell.getX() + 2.0f, y - 2.0f, cell.getWidth() - 4.0f, 4.0f);
                g.setColour (colour::line);
                g.drawHorizontalLine (static_cast<int> (cell.getCentreY()), cell.getX(), cell.getRight());
                // velocity under the cell, gate as a square
                g.setColour (gate ? colour::ember.withAlpha (static_cast<float> (0.3 + 0.7 * vel)) : colour::line);
                g.fillRect (cell.getX() + 2.0f, h + 4.0f, cell.getWidth() - 4.0f, 12.0f);
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
            const int s = juce::jlimit (0, 15, static_cast<int> (e.position.x / (static_cast<float> (getWidth()) / 16.0f)));
            const auto id = std::to_string (s + 1);
            const float h = static_cast<float> (getHeight()) - 20.0f;
            if (e.position.y > h)
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
            depth.setSliderStyle (juce::Slider::LinearHorizontal);
            depth.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            depth.setRange (-1.0, 1.0);
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
            g.setColour (colour::bone);
            g.setFont (text (11.0f, 500));
            g.drawText ((from != nullptr ? juce::String (session.document().displayName (session.registry(), *from)) : juce::String()) + "  >  " + c->toPort,
                        getLocalBounds().removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (colour::boneDim);
            g.drawText (juce::String (juce::roundToInt (depth.getValue() * 100.0)) + " %", getLocalBounds().removeFromTop (16), juce::Justification::centredRight);
        }
        void resized() override
        {
            auto r = getLocalBounds().withTrimmedTop (16);
            remove.setBounds (r.removeFromRight (24).reduced (2));
            depth.setBounds (r);
        }

    private:
        Session& session;
        std::string id;
        juce::Slider depth;
        juce::TextButton remove { "X" };
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
            title = g->name;
            subtitle = "GROUP  " + juce::String (static_cast<int> (edit::groupMembers (doc, id).size())) + " modules, flattened for the budgets";
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
        title = doc.displayName (session.registry(), *n);
        subtitle = juce::String (d->name) + "  " + (n->domain == Domain::voice ? "PER VOICE" : (n->domain == Domain::event ? "NOTES" : "GLOBAL"));
        help = d->help.empty() ? d->description : d->help;
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
            auto c = makeControl (session, target, p);
            addAndMakeVisible (*c);
            if (p.curve == ParamCurve::choice)
                switchNames.push_back (p.name);
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
        const int w = space::detailWidth - 16;
        int y = 64;
        helpHeight = 16 + 15 * (1 + static_cast<int> (juce::GlyphArrangement::getStringWidth (text (12.0f), help) / static_cast<float> (w - 16)));
        y += helpHeight + 8;
        for (auto& b : buttons)
        {
            b->setBounds (8, y, w - 16, 28);
            y += 34;
        }
        for (auto& l : labels)
        {
            l->setBounds (8, y, w - 16, 24);
            y += 28;
        }
        if (! switches.empty())
        {
            y += 4;
            sectionY.push_back ({ y, "MODES" });
            y += 18;
            int col = 0;
            for (std::size_t i = 0; i < switches.size(); ++i)
            {
                // a choice gets its name above it; a toggle carries its own
                const bool named = i < switchNames.size() && switchNames[i].isNotEmpty();
                const int x = 8 + col * (w - 16) / 2;
                if (named)
                    switchLabels.push_back ({ juce::Rectangle<int> (x, y, (w - 16) / 2 - 6, 13), switchNames[i] });
                switches[i]->setBounds (x, y + 14, (w - 16) / 2 - 6, 24);
                if (++col == 2)
                {
                    col = 0;
                    y += 44;
                }
            }
            if (col != 0)
                y += 44;
        }
        if (! knobs.empty())
        {
            sectionY.push_back ({ y + 4, "CONTROLS" });
            y += 22;
            int col = 0;
            const int cw = (w - 16) / 4;
            for (auto& c : knobs)
            {
                c->setBounds (8 + col * cw, y, cw, 64);
                if (++col == 4)
                {
                    col = 0;
                    y += 70;
                }
            }
            if (col != 0)
                y += 70;
        }
        if (steps != nullptr)
        {
            sectionY.push_back ({ y + 4, "STEPS  drag pitch, Shift-drag velocity, click the row below for gates" });
            y += 22;
            steps->setBounds (8, y, w - 16, 120);
            y += 128;
        }
        if (! mods.empty())
        {
            sectionY.push_back ({ y + 4, "MODULATED BY" });
            y += 22;
            for (auto& m : mods)
            {
                m->setBounds (8, y, w - 16, 40);
                y += 44;
            }
        }
        setSize (w, y + 16);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::bone);
        g.setFont (text (18.0f, 600));
        g.drawText (title, 8, 8, getWidth() - 16, 24, juce::Justification::centredLeft);
        g.setColour (colour::ember);
        g.setFont (text (10.0f, 600));
        g.drawText (subtitle, 8, 34, getWidth() - 16, 16, juce::Justification::centredLeft);
        g.setColour (colour::boneDim);
        g.setFont (text (12.0f));
        g.drawFittedText (help, 8, 60, getWidth() - 16, helpHeight, juce::Justification::topLeft, 8, 1.0f);
        g.setFont (text (9.5f, 500));
        g.setColour (colour::boneDim);
        for (const auto& [area, name] : switchLabels)
            g.drawText (name, area, juce::Justification::centredLeft);
        g.setFont (text (9.5f, 600));
        for (const auto& [y, name] : sectionY)
        {
            g.setColour (colour::boneFaint);
            g.drawText (name, 8, y, getWidth() - 16, 14, juce::Justification::centredLeft);
            g.setColour (colour::line);
            g.drawHorizontalLine (y - 3, 8.0f, static_cast<float> (getWidth() - 8));
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
    int helpHeight = 40;
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
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
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
    g.fillAll (colour::card);
    g.setColour (colour::line);
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));
    if (content == nullptr)
    {
        g.setColour (colour::boneFaint);
        g.setFont (text (12.5f));
        g.drawFittedText ("Select a module to see every control.\nDouble-click a module, or press Return.", getLocalBounds().reduced (24), juce::Justification::centred, 4);
    }
}

void DetailPanel::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (1));
    close.setBounds (getWidth() - 34, 8, 26, 24);
}

} // namespace andor::modular::ui
