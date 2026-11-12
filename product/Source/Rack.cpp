#include "Rack.h"

#include "Frame.h"
#include "PlayView.h"

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
            g.setColour (colour::text);
            g.setFont (fonts::make (11.5f, Weight::medium, 0.04f));
            g.drawText ((from != nullptr ? juce::String (session.document().displayName (session.registry(), *from)) : juce::String()).toUpperCase() + "   >   "
                            + juce::String (c->toPort).toUpperCase(),
                        getLocalBounds().removeFromTop (16).withTrimmedRight (50), juce::Justification::centredLeft, true);
            g.setColour (colour::textSecondary);
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
/** The rack's centre: the module's controls in sections side by side (as tall as the rack). */
class Rack::Content final : public juce::Component
{
public:
    Content (Session& s, const std::string& id, juce::Colour arc) : session (s), target (id)
    {
        const auto& doc = session.document();
        if (const auto* g = doc.findGroup (id))
        {
            for (std::size_t c = 0; c < g->controls.size(); ++c)
            {
                auto name = std::make_unique<juce::Label> ("c" + std::to_string (c), g->controls[c].name);
                name->setEditable (true);
                name->setColour (juce::Label::textColourId, colour::text);
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
            return;
        }
        const auto* n = doc.findNode (id);
        const auto* d = n != nullptr ? session.registry().find (n->type) : nullptr;
        if (d == nullptr)
            return;
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
                knob->setInks (colour::textSecondary, colour::text, arc);
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
    }

    /** Sections side by side, each as tall as the rack: a module's whole surface at once. */
    void layout (int h, int available)
    {
        sections.clear();
        switchLabels.clear();
        const int top = 28, gap = 26;
        int x = 2;
        auto begin = [&] (const juce::String& name) {
            if (! sections.empty())
                x += gap;
            sections.push_back ({ x, name });
        };
        auto columns = [] (std::size_t count, int rows) { return (static_cast<int> (count) + rows - 1) / rows; };
        if (! labels.empty())
        {
            begin ("CONTROL NAMES");
            const int rows = std::max (1, (h - top) / 30), cw = 176;
            for (std::size_t i = 0; i < labels.size(); ++i)
                labels[i]->setBounds (x + (static_cast<int> (i) / rows) * cw, top + (static_cast<int> (i) % rows) * 30, cw - 8, 26);
            x += columns (labels.size(), rows) * cw - 8;
        }
        if (! switches.empty())
        {
            begin ("MODES");
            const int rows = std::max (1, (h - top + 4) / 48), cw = 150;
            for (std::size_t i = 0; i < switches.size(); ++i)
            {
                // a choice gets its name above it; a toggle is a key that carries its own
                const int cx = x + (static_cast<int> (i) / rows) * cw, cy = top + (static_cast<int> (i) % rows) * 48;
                if (i < switchNames.size() && switchNames[i].isNotEmpty())
                    switchLabels.push_back ({ juce::Rectangle<int> (cx + 2, cy, cw - 10, 13), switchNames[i] });
                switches[i]->setBounds (cx, cy + 14, cw - 8, 28);
            }
            x += columns (switches.size(), rows) * cw - 8;
        }
        if (! knobs.empty())
        {
            begin ("CONTROLS");
            // (row by row: read left to right, as on the tile)
            const int rows = std::max (1, (h - top + 6) / 96), cw = 74, cols = columns (knobs.size(), rows);
            for (std::size_t i = 0; i < knobs.size(); ++i)
                knobs[i]->setBounds (x + (static_cast<int> (i) % cols) * cw, top + (static_cast<int> (i) / cols) * 96, cw, 90);
            x += cols * cw;
        }
        if (steps != nullptr)
        {
            begin ("STEPS");
            // (the room that is left, so the sixteen steps are in sight without scrolling)
            const int w = juce::jlimit (320, 560, available - x - 8);
            steps->setBounds (x, top, w, h - top - 22);
            stepsHint = { x, h - 18, w, 14 };
            x += w;
        }
        if (! mods.empty())
        {
            begin ("MODULATED BY");
            const int rows = std::max (1, (h - top) / 48), cw = 236;
            for (std::size_t i = 0; i < mods.size(); ++i)
                mods[i]->setBounds (x + (static_cast<int> (i) / rows) * cw, top + (static_cast<int> (i) % rows) * 48, cw - 12, 44);
            x += columns (mods.size(), rows) * cw - 12;
        }
        setSize (x + 6, h);
    }

    void paint (juce::Graphics& g) override
    {
        // Ceramic keys (the toggles) are drawn by the surface they sit on.
        skin::drawKeys (g, *this);
        g.setFont (fonts::make (10.5f, Weight::medium, 0.06f));
        g.setColour (colour::textSecondary);
        for (const auto& [area, name] : switchLabels)
            g.drawText (name.toUpperCase(), area, juce::Justification::centredLeft, true);
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& [x, name] = sections[i];
            if (i > 0)
            {
                g.setColour (colour::divider);
                g.fillRect (x - 13, 6, 1, getHeight() - 12);
            }
            g.setColour (colour::textSecondary.withAlpha (0.8f));
            g.setFont (fonts::make (10.5f, Weight::medium, 0.14f));
            g.drawText (name, x + 2, 4, 200, 14, juce::Justification::centredLeft, false);
        }
        if (steps != nullptr)
        {
            g.setColour (colour::textSecondary.withAlpha (0.8f));
            g.setFont (type::micro (10.5f));
            g.drawText ("drag: pitch    Shift + drag: velocity    lower row: gates", stepsHint, juce::Justification::centredLeft, false);
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

    int controlCount() const { return static_cast<int> (knobs.size() + switches.size()); }

private:
    Session& session;
    std::string target;
    std::vector<std::unique_ptr<juce::Component>> knobs, switches, labels, mods;
    std::unique_ptr<juce::Component> steps;
    std::vector<std::pair<int, juce::String>> sections;
    std::vector<juce::String> switchNames;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> switchLabels;
    juce::Rectangle<int> stepsHint;
};

//==============================================================================
Rack::Rack (Session& s) : session (s)
{
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (7);
    viewport.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, board::column);
    addAndMakeVisible (viewport);
    close.setIcon (icon::Kind::close);
    close.setInk (colour::textSecondary);
    close.setTooltip ("Close the rack (Esc)");
    close.setTitle ("Close the rack");
    close.onClick = [this] {
        if (onClose)
            onClose();
        else
            show ({});
    };
    addAndMakeVisible (close);
    setTitle ("Rack");
}

Rack::~Rack() { viewport.setViewedComponent (nullptr, false); }

void Rack::show (const std::string& id)
{
    if (id != current)
        viewport.setViewPosition (0, 0);
    current = id;
    rebuild();
}

int Rack::controlCount() const { return content != nullptr ? content->controlCount() : 0; }
osp::plugin::rack::Module* Rack::module() const { return family != nullptr ? family->module (osp::plugin::rack::Kind::fx) : nullptr; }
bool Rack::scrolls() const { return content != nullptr && content->getWidth() > viewport.getWidth(); }

void Rack::rebuild()
{
    const auto& doc = session.document();
    const auto* g = doc.findGroup (current);
    const auto* n = doc.findNode (current);
    // A native rack is the family's component: it follows its module by itself (a mode chosen
    // in it changes the patch, and must not tear down the control that was just clicked). It is
    // rebuilt only when it shows another module, or its module became another type.
    if (family != nullptr && n != nullptr && n->type == shownType && native->host->nodeId() == current)
    {
        native->host->pull();
        return;
    }
    const int scroll = viewport.getViewPositionX();
    viewport.setViewedComponent (nullptr, false);
    content.reset();
    family.reset();
    native.reset();
    shownType.clear();
    buttons.clear();
    title = subtitle = help = {};
    if (current.empty() || (g == nullptr && n == nullptr))
    {
        current.clear();
        stopTimer();
        repaint();
        return;
    }
    if (n != nullptr)
        if (auto made = createNativeRack (session, current))
        {
            native = std::move (made);
            shownType = n->type;
            family = std::make_unique<osp::plugin::rack::Host> ([this] (osp::plugin::rack::Kind) { return native->make(); });
            family->setFullBleed (true);
            addAndMakeVisible (*family);
            family->setShown (osp::plugin::rack::Kind::fx, true);
            // its X (the rail's): the drawer closes (after the click has returned)
            family->onLayoutChanged = [this] {
                if (family != nullptr && family->shownKinds().empty())
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Rack> (this)] {
                        if (safe != nullptr && safe->onClose)
                            safe->onClose();
                    });
            };
            viewport.setVisible (false);
            close.setVisible (false);
            resized();
            family->settle();
            startTimerHz (30);
            repaint();
            return;
        }
    viewport.setVisible (true);
    close.setVisible (true);
    auto button = [this] (const juce::String& text, std::function<void()> action) {
        auto b = std::make_unique<juce::TextButton> (text);
        b->onClick = std::move (action);
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    };
    group = g != nullptr;
    if (group)
    {
        look = groupLook();
        title = juce::String (g->name);
        subtitle = "GROUP   " + juce::String (static_cast<int> (edit::groupMembers (doc, current).size())) + " MODULES";
        help = "A group plays as its modules (each still counts against the patch's limits). Its controls are on its card and map to the macros. Double-click its card to open it.";
        button ("SAVE AS MODULE...", [this] {
            auto chooser = std::make_shared<juce::FileChooser> ("Save module", Session::userDirectory().getChildFile ("Modules"), "*.andormodule");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                f = f.withFileExtension ("andormodule");
                f.getParentDirectory().createDirectory();
                if (f.replaceWithText (juce::String (edit::exportModule (session.document(), current))))
                    session.message ("saved module " + f.getFileName());
            });
        });
    }
    else
    {
        const auto* d = session.registry().find (n->type);
        int ordinal = -1, sources = 0;
        for (const auto& other : doc.nodes)
            if (const auto* od = session.registry().find (other.type); od != nullptr && od->role == NodeRole::source)
            {
                if (other.id == current)
                    ordinal = sources;
                ++sources;
            }
        look = lookFor (n->type, ordinal);
        if (d == nullptr)
        {
            title = juce::String ("MISSING ") + n->type;
            help = "This module is not part of this version. It is kept, silent, with its settings, so a newer version can play it again.";
        }
        else
        {
            title = juce::String (doc.displayName (session.registry(), *n));
            subtitle = juce::String (d->name) + "    " + (n->domain == Domain::voice ? "PER VOICE" : (n->domain == Domain::event ? "NOTES" : "GLOBAL"));
            help = juce::String (d->help.empty() ? d->description : d->help);
            if (n->strings.count ("sample") > 0)
                button (n->strings.at ("sample").empty() ? "CHOOSE A RECORDING..." : "REPLACE THE RECORDING...", [this] {
                    auto chooser = std::make_shared<juce::FileChooser> ("Choose a recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
                        if (fc.getResult() != juce::File())
                            session.setNodeString (current, "sample", session.keyFor (fc.getResult()));
                    });
                });
            if (n->type == "src.external")
            {
                button ("CHOOSE PLUGIN...", [this] {
                    auto chooser = std::make_shared<juce::FileChooser> ("Choose an instrument plugin", juce::File(), "*.vst3;*.component");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                          [this, chooser] (const juce::FileChooser& fc) {
                                              if (fc.getResult() != juce::File())
                                                  session.setNodeString (current, "plugin", fc.getResult().getFullPathName().toStdString());
                                          });
                });
                button ("OPEN ITS WINDOW", [this] {
                    if (! session.externals().openEditor (current))
                        session.message ("no plugin window: " + session.externals().status (current), false, true);
                });
            }
        }
    }
    content = std::make_unique<Content> (session, current, look.face == ModuleLook::Face::card || look.face == ModuleLook::Face::panel ? look.arc : colour::accent);
    viewport.setViewedComponent (content.get(), false);
    resized();
    viewport.setViewPosition (scroll, 0);
    startTimerHz (15);
    repaint();
}

void Rack::refreshValues()
{
    if (native != nullptr)
        native->host->pull();
    if (content != nullptr)
        content->refresh();
}

void Rack::timerCallback()
{
    if (family != nullptr)
    {
        if (isVisible())
            family->refresh();   // (the family's racks pull their modes and pictures on this beat)
        return;
    }
    // its display follows the running graph
    if (isVisible() && isOpen())
        repaint (display);
}

void Rack::paint (juce::Graphics& g)
{
    if (! isOpen())
        return;
    if (family != nullptr)
    {
        // The device is its own face (full bleed): only its shadow on the board is drawn here.
        const auto face = family->getBounds().toFloat().withTrimmedBottom (static_cast<float> (osp::plugin::rack::Host::bottomMargin));
        juce::Path shape;
        shape.addRoundedRectangle (face, design::layout::panelRadius);
        design::CachedShadow (juce::Colour (0x30241d17), 10, { 0, 3 }).drawForPath (g, shape);
        return;
    }
    // A drawer of the board: the family's raised cream card across its foot.
    draw::raised (g, card.toFloat(), design::layout::cardRadius, colour::cardTop, colour::cardBottom);

    // Left: what it is.
    auto id = identity;
    const auto chip = id.removeFromTop (38).removeFromLeft (38).toFloat().reduced (1.0f);
    if (look.badge >= 0)
        paintBadge (g, chip.reduced (2.0f), look.badge, look.badgeLetter);
    else
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (chip, 7.0f);
        g.reduceClipRegion (clip);
        paintFace (g, chip.withWidth (tile::width), look, false, false);
    }
    g.setColour (colour::text);
    g.setFont (fonts::make (17.0f, Weight::bold, 0.05f));
    g.drawFittedText (title.toUpperCase(), identity.getX() + 48, identity.getY(), identity.getWidth() - 48, 21, juce::Justification::centredLeft, 1, 0.7f);
    g.setColour (colour::textSecondary);
    g.setFont (fonts::make (10.0f, Weight::medium, 0.12f));
    g.drawText (subtitle.toUpperCase(), identity.getX() + 48, identity.getY() + 22, identity.getWidth() - 48, 14, juce::Justification::centredLeft, true);
    const int helpBottom = identity.getBottom() - static_cast<int> (buttons.size()) * 34 - (buttons.empty() ? 0 : 4);
    g.setFont (type::micro (12.0f));
    g.drawFittedText (help, identity.getX(), identity.getY() + 48, identity.getWidth(), std::max (0, helpBottom - identity.getY() - 48), juce::Justification::topLeft, 8, 0.9f);
    g.setColour (colour::divider);
    g.fillRect (identity.getRight() + 13, card.getY() + 14, 1, card.getHeight() - 28);

    // Right: what it is doing now.
    g.setColour (colour::textSecondary.withAlpha (0.8f));
    g.setFont (fonts::make (10.5f, Weight::medium, 0.14f));
    g.drawText ("LIVE", display.getX() + 2, card.getY() + 12, 80, 14, juce::Justification::centredLeft, false);
    if (look.badge >= 0 && ! group)
    {
        // a source: the front panel's own display of it
        draw::well (g, display.toFloat(), 9.0f, colour::wellA);
        paintSourceDisplay (g, display.toFloat(), session, current, look, true, false);
    }
    else
    {
        paintGlass (g, display.toFloat(), look);
        paintModuleDisplay (g, display.toFloat().reduced (8.0f, 14.0f), session, current, group, look, 2.4f);
    }
}

void Rack::resized()
{
    if (family != nullptr)
    {
        family->setBounds (getLocalBounds().reduced (10, 0).withTrimmedTop (4).withHeight (juce::roundToInt (family->height())));
        return;
    }
    card = getLocalBounds().reduced (10, 0).withTrimmedTop (5).withTrimmedBottom (9);
    auto inner = card.reduced (18, 12);
    identity = inner.removeFromLeft (232);
    auto right = inner.removeFromRight (252);
    close.setBounds (card.getRight() - 40, card.getY() + 5, 32, 28);
    display = right.withTrimmedTop (22);
    inner.removeFromLeft (27);
    inner.removeFromRight (20);
    viewport.setBounds (inner.withTrimmedTop (-4).withBottom (card.getBottom() - 5));
    if (content != nullptr)
        content->layout (viewport.getHeight() - 9, viewport.getWidth());   // (room for the scroll bar, there or not: nothing jumps)
    int y = identity.getBottom() - static_cast<int> (buttons.size()) * 34 + 4;
    for (auto& b : buttons)
    {
        b->setBounds (identity.getX(), y, identity.getWidth(), 30);
        y += 34;
    }
}

} // namespace andor::modular::ui
