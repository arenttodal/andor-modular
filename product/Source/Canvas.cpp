#include "Canvas.h"

#include <cmath>

namespace andor::modular::ui
{

namespace
{
    constexpr int knobW = 42;

    const CompiledNode* liveNode (Session& s, const std::string& id)
    {
        auto* g = s.graph();
        if (g == nullptr)
            return nullptr;
        const auto it = g->nodeIndexById.find (id);
        return it == g->nodeIndexById.end() ? nullptr : &g->nodes[static_cast<std::size_t> (it->second)];
    }

    float tel (const CompiledNode* n, int i)
    {
        return n != nullptr && i < n->telemetryCount.load (std::memory_order_relaxed) ? n->telemetry[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

    /** A group's own control on its card (moves the group control channel live): the same
        mini knob as a module's. */
    class GroupKnob final : public juce::Slider
    {
    public:
        GroupKnob (Session& s, std::string g, int c) : session (s), group (std::move (g)), control (c)
        {
            setSliderStyle (RotaryHorizontalVerticalDrag);
            setRotaryParameters (LookAndFeel::rotaryStart, LookAndFeel::rotaryEnd, true);
            setTextBoxStyle (NoTextBox, true, 0, 0);
            setRange (0.0, 1.0);
            getProperties().set ("mini", true);
            getProperties().set ("arc", static_cast<juce::int64> (board::bone.getARGB()));
            refresh();
        }
        void refresh()
        {
            if (const auto* gm = session.document().findGroup (group); gm != nullptr && control < static_cast<int> (gm->controls.size()))
                setValue (gm->controls[static_cast<std::size_t> (control)].value, juce::dontSendNotification);
        }
        void startedDragging() override { session.beginGesture ("group control"); }
        void valueChanged() override
        {
            auto* gm = session.document().findGroup (group);
            if (gm == nullptr || control >= static_cast<int> (gm->controls.size()))
                return;
            gm->controls[static_cast<std::size_t> (control)].value = getValue();
            if (auto* g = session.graph())
                g->setChannel (g->channelIndex (group, control), static_cast<float> (getValue()));
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            const auto* gm = session.document().findGroup (group);
            const auto caption = r.removeFromBottom (13);
            const auto area = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), r.getHeight());
            const auto params = getRotaryParameters();
            getLookAndFeel().drawRotarySlider (g, area.getX(), area.getY(), area.getWidth(), area.getHeight(), static_cast<float> (getValue()), params.startAngleRadians,
                                               params.endAngleRadians, *this);
            g.setColour (board::bone.withAlpha (isMouseOverOrDragging() ? 1.0f : 0.74f));
            g.setFont (fonts::make (9.5f, Weight::medium, 0.05f));
            g.drawFittedText (isMouseOverOrDragging() ? juce::String (juce::roundToInt (getValue() * 100.0)) + " %"
                                                      : (gm != nullptr && control < static_cast<int> (gm->controls.size()) ? juce::String (gm->controls[static_cast<std::size_t> (control)].name)
                                                                                                                             : juce::String()),
                              caption, juce::Justification::centred, 1, 0.75f);
        }

    private:
        Session& session;
        std::string group;
        int control = 0;
    };
}

//==============================================================================
NodeView::NodeView (Canvas& owner, std::string id, bool isGroupCard) : canvas (owner), nodeId (std::move (id)), group (isGroupCard)
{
    setWantsKeyboardFocus (true);
    // The face (its enamel and motif, its ceramic) is drawn once and kept; only what moves
    // on it (the glass, a turned knob) is drawn again.
    setBufferedToImage (true);
    rebuild();
}

bool NodeView::isSource() const noexcept
{
    if (group)
        return false;
    const auto* n = canvas.session().document().findNode (nodeId);
    const auto* d = n != nullptr ? canvas.session().registry().find (n->type) : nullptr;
    return d != nullptr && d->role == NodeRole::source;
}

void NodeView::rebuild()
{
    auto& s = canvas.session();
    const auto& doc = s.document();
    controls.clear();
    portList.clear();
    int rowsIn = 0, rowsOut = 0;
    if (group)
    {
        const auto* g = doc.findGroup (nodeId);
        if (g == nullptr)
            return;
        look = groupLook();
        for (const auto& p : g->ports)
        {
            Port port;
            port.id = p.port;
            port.name = p.name;
            port.input = p.input;
            port.innerNode = p.node;
            const auto* inner = doc.findNode (p.node);
            const auto* d = inner != nullptr ? s.registry().find (inner->type) : nullptr;
            if (d != nullptr)
            {
                const int i = p.input ? d->inputIndex (p.port) : d->outputIndex (p.port);
                if (i >= 0)
                    port.kind = effectiveKind ((p.input ? d->inputs : d->outputs)[static_cast<std::size_t> (i)], inner->domain);
                else if (p.input && d->paramIndex (p.port) >= 0)
                    port.kind = PortKind::control;
            }
            (p.input ? rowsIn : rowsOut)++;
            portList.push_back (port);
        }
        for (int c = 0; c < static_cast<int> (g->controls.size()) && c < 4; ++c)
        {
            auto k = std::make_unique<GroupKnob> (s, nodeId, c);
            addAndMakeVisible (*k);
            controls.push_back (std::move (k));
        }
    }
    else
    {
        const auto* n = doc.findNode (nodeId);
        const auto* d = n != nullptr ? s.registry().find (n->type) : nullptr;
        look = lookFor (n != nullptr ? n->type : std::string(), canvas.sourceOrdinal (nodeId));
        if (d != nullptr)
        {
            for (const auto& p : d->inputs)
            {
                portList.push_back ({ p.id, p.name, effectiveKind (p, n->domain), true, {}, {} });
                ++rowsIn;
            }
            for (const auto& p : d->outputs)
            {
                if (d->role == NodeRole::output)
                    continue;   // OUTPUT's signal goes to the host
                portList.push_back ({ p.id, p.name, effectiveKind (p, n->domain), false, {}, {} });
                ++rowsOut;
            }
            int knobs = 0, choices = 0;
            for (const auto& p : d->params)
            {
                if (! p.quick || ! d->paramShown (p, n->params))
                    continue;
                const bool isChoice = p.curve == ParamCurve::choice || p.curve == ParamCurve::toggle;
                if (isChoice && choices >= 1)
                    continue;
                if (! isChoice && knobs >= 4)
                    continue;
                (isChoice ? choices : knobs)++;
                std::unique_ptr<juce::Component> c;
                if (p.curve == ParamCurve::choice)
                {
                    auto mode = std::make_unique<ParamMode> (s, nodeId, p.id);
                    mode->setInks (look.mutedInk, look.ink);
                    c = std::move (mode);
                }
                else if (p.curve == ParamCurve::toggle)
                {
                    auto toggle = std::make_unique<ParamToggle> (s, nodeId, p.id, true);
                    toggle->setInks (look.mutedInk, look.face == ModuleLook::Face::enamel ? look.ink : look.arc);
                    c = std::move (toggle);
                }
                else
                {
                    auto knob = std::make_unique<ParamKnob> (s, nodeId, p.id, ParamKnob::Style::tile);
                    knob->setInks (look.mutedInk, look.ink, look.arc);
                    c = std::move (knob);
                }
                addAndMakeVisible (*c);
                controls.push_back (std::move (c));
            }
        }
    }
    const int rows = std::max (rowsIn, rowsOut);
    int knobCount = 0;
    for (auto& c : controls)
        knobCount += dynamic_cast<juce::Slider*> (c.get()) != nullptr ? 1 : 0;
    const int height = tile::top + rows * tile::portRow + (rows > 0 ? 4 : 0) + (knobCount > 0 ? tile::knobRow : 0) + tile::glass + 10;
    setSize (tile::width + 2 * tile::pad, height + 2 * tile::pad);
    int yi = tile::pad + tile::top + tile::portRow / 2 + 1, yo = yi;
    for (auto& p : portList)
    {
        if (p.input)
        {
            p.at = { static_cast<float> (tile::pad), static_cast<float> (yi) };
            yi += tile::portRow;
        }
        else
        {
            p.at = { static_cast<float> (tile::pad + tile::width), static_cast<float> (yo) };
            yo += tile::portRow;
        }
    }
    resized();
    const auto* n = doc.findNode (nodeId);
    setTitle (group ? juce::String (doc.findGroup (nodeId)->name) : (n != nullptr ? juce::String (doc.displayName (s.registry(), *n)) : juce::String()));
    repaint();
}

void NodeView::resized()
{
    int rowsIn = 0, rowsOut = 0;
    for (const auto& p : portList)
        (p.input ? rowsIn : rowsOut)++;
    const int rows = std::max (rowsIn, rowsOut);
    face = getLocalBounds().reduced (tile::pad);
    // The name band: what stands left of the name (a device's motif, a source's badge), the
    // name, under it the mode; the status light at its right end.
    const int nameX = look.face == ModuleLook::Face::enamel ? 52 : (look.badge >= 0 ? 40 : 13);
    header = face.withHeight (tile::top);
    ledArea = juce::Rectangle<int> (face.getRight() - 24, face.getY() + 5, 20, 20);
    nameArea = juce::Rectangle<int> (face.getX() + nameX, face.getY() + 6, face.getWidth() - nameX - 26, 17);
    muteArea = soloArea = {};
    if (group || isSource())
    {
        // M and S: two small keys before the light (a group has no light: at the end).
        const int right = group ? face.getRight() - 8 : ledArea.getX() - 1;
        soloArea = juce::Rectangle<int> (right - 17, face.getY() + 8, 17, 15);
        muteArea = soloArea.translated (-19, 0);
        nameArea.setRight (muteArea.getX() - 4);
    }
    modeArea = juce::Rectangle<int> (face.getX() + nameX, face.getY() + 22, face.getWidth() - nameX - 10, 15);
    auto area = face.withTrimmedTop (tile::top + rows * tile::portRow + (rows > 0 ? 4 : 0)).reduced (8, 0);
    std::vector<juce::Component*> knobs;
    hasMode = false;
    for (auto& c : controls)
    {
        if (dynamic_cast<juce::Slider*> (c.get()) != nullptr)
            knobs.push_back (c.get());
        else
        {
            c->setBounds (modeArea);
            hasMode = true;
        }
    }
    if (! knobs.empty())
    {
        auto row = area.removeFromTop (tile::knobRow);
        const int w = row.getWidth() / 4;
        const int startX = row.getX() + (row.getWidth() - w * static_cast<int> (knobs.size())) / 2;
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (startX + w * static_cast<int> (i), row.getY(), w, row.getHeight()).withSizeKeepingCentre (std::min (w, knobW), row.getHeight() - 2));
    }
    telemetryArea = area.removeFromTop (tile::glass);
}

void NodeView::refreshControls()
{
    for (auto& c : controls)
    {
        if (auto* gk = dynamic_cast<GroupKnob*> (c.get()))
            gk->refresh();
        else
            refreshControl (c.get());
    }
}

void NodeView::repaintLive()
{
    if (! telemetryArea.isEmpty())
        repaint (telemetryArea);
}

int NodeView::portAt (juce::Point<float> local) const
{
    for (std::size_t i = 0; i < portList.size(); ++i)
        if (portList[i].at.getDistanceFrom (local) < 9.0f)
            return static_cast<int> (i);
    return -1;
}

juce::Point<float> NodeView::portPosition (const std::string& node, const std::string& port, bool input) const
{
    for (const auto& p : portList)
        if (p.input == input && p.id == port && (! group || p.innerNode == node))
            return p.at;
    // a group card without that port exposed: its edge
    return { static_cast<float> (input ? tile::pad : tile::pad + tile::width), static_cast<float> (tile::pad + tile::top / 2) };
}

bool NodeView::hitTest (int x, int y)
{
    // The room around the face belongs to the board, except where a socket sits on its edge.
    if (face.contains (x, y))
        return true;
    return portAt ({ static_cast<float> (x), static_cast<float> (y) }) >= 0;
}

void NodeView::paint (juce::Graphics& g)
{
    auto& s = canvas.session();
    const auto& doc = s.document();
    const bool sel = canvas.selection().count (nodeId) > 0;
    const auto r = face.toFloat();
    const auto* n = group ? nullptr : doc.findNode (nodeId);
    const auto* d = n != nullptr ? s.registry().find (n->type) : nullptr;
    const bool enabled = group || (n != nullptr && n->enabled);
    const bool missing = ! group && d == nullptr;

    // Silent by M, by another source's S, or bypassed: the face under its veil, truthfully.
    bool muted = false, soloed = false, audible = enabled;
    if (group)
    {
        const auto* gm = doc.findGroup (nodeId);
        muted = gm != nullptr && gm->muted;
        soloed = gm != nullptr && gm->solo;
    }
    else if (n != nullptr)
    {
        muted = n->muted;
        soloed = n->solo;
        audible = enabled && (d == nullptr || audibleGain (doc, s.registry(), *n) > 0.0);
    }
    paintFace (g, r, look, hover, ! audible);
    if (sel)
    {
        // The routing board's selection ring.
        g.setColour (board::sage);
        g.drawRoundedRectangle (r.expanded (3.0f), tile::radius + 3.0f, 2.0f);
    }

    // The name band.
    juce::String title, under;
    if (group)
    {
        const auto* gm = doc.findGroup (nodeId);
        title = gm != nullptr ? juce::String (gm->name) : juce::String ("GROUP");
        under = juce::String (static_cast<int> (edit::groupMembers (doc, nodeId).size())) + " MODULES";
    }
    else if (n != nullptr)
    {
        title = juce::String (doc.displayName (s.registry(), *n));
        under = n->domain == Domain::voice ? "PER VOICE" : (n->domain == Domain::event ? "NOTES" : "GLOBAL");
    }
    if (look.badge >= 0)
        paintBadge (g, juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 9.0f, 23.0f, 23.0f), look.badge, look.badgeLetter);
    g.setColour (look.ink.withMultipliedAlpha (enabled ? 1.0f : 0.6f));
    g.setFont (fonts::make (13.5f, Weight::bold, 0.06f));
    g.drawFittedText (title.toUpperCase(), nameArea, juce::Justification::centredLeft, 1, 0.7f);
    if (! hasMode)
    {
        g.setColour (look.mutedInk.withMultipliedAlpha (enabled ? 1.0f : 0.7f));
        g.setFont (fonts::make (10.5f, Weight::medium, 0.07f));
        g.drawFittedText (under, modeArea, juce::Justification::centredLeft, 1, 0.75f);
    }
    if (! group)
        paintLight (g, ledArea.toFloat(), look, enabled);
    if (! muteArea.isEmpty())
    {
        // The family's small modifier keys: lit in the accent's soft face while on.
        auto key = [&] (juce::Rectangle<int> area, const char* letter, bool on, bool hot) {
            const auto k = area.toFloat();
            if (look.face == ModuleLook::Face::stack)
            {
                g.setColour (on ? board::bone : board::bone.withAlpha (hot ? 0.22f : 0.12f));
                g.fillRoundedRectangle (k, 3.5f);
                g.setColour (on ? juce::Colour (0xff4a4640) : board::bone.withAlpha (0.85f));
            }
            else
            {
                draw::button (g, k, 3.5f, on, hot, colour::accentSoft);
                g.setColour (on ? colour::text : colour::textSecondary);
            }
            g.setFont (fonts::make (9.0f, Weight::semibold, 0.02f));
            g.drawText (letter, area, juce::Justification::centred, false);
        };
        key (muteArea, "M", muted, hoverKey == 1);
        key (soloArea, "S", soloed, hoverKey == 2);
    }

    // Sockets on the face's edges, their names inside.
    std::set<std::string> connectedIn, connectedOut;
    for (const auto& c : doc.connections)
    {
        if (c.to == nodeId || (group && std::any_of (portList.begin(), portList.end(), [&] (const Port& p) { return p.innerNode == c.to && p.id == c.toPort; })))
            connectedIn.insert ((group ? c.to + "/" : std::string()) + c.toPort);
        if (c.from == nodeId || (group && std::any_of (portList.begin(), portList.end(), [&] (const Port& p) { return p.innerNode == c.from && p.id == c.fromPort; })))
            connectedOut.insert ((group ? c.from + "/" : std::string()) + c.fromPort);
    }
    g.setFont (fonts::make (9.5f, Weight::medium, 0.06f));
    for (std::size_t i = 0; i < portList.size(); ++i)
    {
        const auto& p = portList[i];
        const auto key = (group ? p.innerNode + "/" : std::string()) + p.id;
        const bool on = p.input ? connectedIn.count (key) > 0 : connectedOut.count (key) > 0;
        drawSocket (g, p.at, p.kind, on, static_cast<int> (i) == hoverPort);
        if (p.input && p.kind != PortKind::notes)
        {
            // Several cables into one input (it hears their sum): how many, beside the socket.
            int feeding = 0;
            for (const auto& c : doc.connections)
                feeding += ! c.toParam && c.toPort == p.id && c.to == (group ? p.innerNode : nodeId) ? 1 : 0;
            if (feeding > 1)
            {
                const auto pill = juce::Rectangle<float> (13.0f, 11.0f).withCentre ({ p.at.x - 1.0f, p.at.y - 11.0f });
                g.setColour (board::terminalFace);
                g.fillRoundedRectangle (pill, 5.0f);
                g.setColour (board::terminalEdge);
                g.drawRoundedRectangle (pill, 5.0f, 0.8f);
                g.setColour (board::terminalInk);
                g.setFont (fonts::make (8.5f, Weight::semibold));
                g.drawText (juce::String (feeding), pill, juce::Justification::centred, false);
            }
        }
        g.setColour (on ? look.ink.withAlpha (0.92f) : look.mutedInk);
        const auto labelArea = p.input ? juce::Rectangle<float> (r.getX() + 10.0f, p.at.y - 8.0f, 78.0f, 16.0f) : juce::Rectangle<float> (r.getRight() - 88.0f, p.at.y - 8.0f, 78.0f, 16.0f);
        g.drawText (juce::String (p.name).toUpperCase(), labelArea, p.input ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }
    if (missing)
    {
        g.setColour (look.ink);
        g.setFont (type::micro (11.0f));
        g.drawFittedText ("Missing module: " + juce::String (n != nullptr ? n->type : std::string()) + " (kept, silent)", telemetryArea.expanded (0, 6), juce::Justification::centredLeft, 2);
        return;
    }
    if (! telemetryArea.isEmpty())
    {
        paintGlass (g, telemetryArea.toFloat(), look);
        paintTelemetry (g, telemetryArea.toFloat());
    }
}

void NodeView::paintTelemetry (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto& s = canvas.session();
    const auto inner = area.reduced (6.0f, 4.0f);
    const auto lit = look.trace, dim = look.trace.withAlpha (0.28f), words = colour::wellText.brighter (0.25f);
    g.setFont (fonts::make (9.5f, Weight::medium, 0.08f));
    auto label = [&] (const juce::String& t, juce::Colour c) {
        g.setColour (c);
        g.drawText (t, inner, juce::Justification::centredLeft, false);
    };
    if (group)
    {
        // A group shows what leaves it: the loudest of its members.
        float level = 0.0f;
        for (const auto& id : edit::groupMembers (s.document(), nodeId))
            if (const auto* live = liveNode (s, id))
                level = std::max (level, live->peak.load (std::memory_order_relaxed));
        g.setColour (dim);
        g.fillRoundedRectangle (inner.withHeight (3.0f).withCentre (inner.getCentre()), 1.5f);
        g.setColour (lit);
        g.fillRoundedRectangle (inner.withHeight (3.0f).withCentre (inner.getCentre()).withWidth (inner.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 1.5f);
        return;
    }
    const auto* n = s.document().findNode (nodeId);
    if (n == nullptr)
        return;
    const auto* live = liveNode (s, nodeId);
    const auto& type = n->type;
    if (type == "event.arp" || type == "event.stepseq" || type == "event.generative" || type == "event.clock")
    {
        const int steps = type == "event.arp" ? 14 : 16;
        const int current = static_cast<int> (tel (live, 0));
        const float w = inner.getWidth() / static_cast<float> (steps);
        for (int i = 0; i < steps; ++i)
        {
            const bool now = (current % steps) == i;
            float h = 0.35f;
            if (type == "event.arp")
            {
                const float note = tel (live, 2 + i);
                h = note >= 0.0f ? 0.25f + 0.7f * juce::jlimit (0.0f, 1.0f, (note - 36.0f) / 48.0f) : 0.08f;
            }
            const auto cell = juce::Rectangle<float> (inner.getX() + w * static_cast<float> (i) + 1.0f, inner.getBottom() - inner.getHeight() * h, w - 2.0f, inner.getHeight() * h);
            g.setColour (now ? lit : dim);
            g.fillRoundedRectangle (cell, 1.0f);
        }
        return;
    }
    if (type == "src.granular")
    {
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getCentreY() - 0.5f, inner.getWidth(), 1.0f));
        const int count = live != nullptr ? live->telemetryCount.load() - 2 : 0;
        g.setColour (lit.withAlpha (0.85f));
        for (int i = 0; i < count; ++i)
            g.fillEllipse (inner.getX() + inner.getWidth() * tel (live, 2 + i) - 1.5f, inner.getY() + 1.0f + static_cast<float> ((i * 5) % 11), 3.0f, 3.0f);
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * tel (live, 1), inner.getY(), 1.2f, inner.getHeight()));
        g.setColour (words);
        g.drawText (juce::String (static_cast<int> (tel (live, 0))) + " GRAINS", inner, juce::Justification::centredRight, false);
        return;
    }
    if (type == "src.organic" || type == "src.rhythm")
    {
        const auto it = n->strings.find ("sample");
        const auto key = it != n->strings.end() ? it->second : std::string();
        const auto asset = key.empty() ? nullptr : s.assets().resolve (s.resolveKey (key).getFullPathName().toStdString());
        if (asset == nullptr || key.empty())
        {
            label ("DROP A RECORDING", colour::arpOnDark);
            return;
        }
        if (asset->status == SampleAsset::Status::missing || asset->status == SampleAsset::Status::failed)
        {
            label (asset->status == SampleAsset::Status::missing ? "MISSING: DROP TO RELINK" : "UNREADABLE", juce::Colour (0xffe28a6c));
            return;
        }
        if (! asset->overview.empty())
        {
            g.setColour (lit.withAlpha (0.6f));
            const float w = inner.getWidth() / static_cast<float> (asset->overview.size());
            for (std::size_t i = 0; i < asset->overview.size(); ++i)
            {
                const float h = std::max (1.0f, inner.getHeight() * asset->overview[i]);
                g.fillRect (inner.getX() + w * static_cast<float> (i), inner.getCentreY() - h * 0.5f, std::max (1.0f, w - 0.5f), h);
            }
        }
        if (type == "src.organic" && tel (live, 0) >= 0.0f && live != nullptr && live->telemetryCount.load() > 0)
        {
            g.setColour (colour::accent.brighter (0.2f));
            g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * tel (live, 0), inner.getY(), 1.2f, inner.getHeight()));
        }
        if (asset->status != SampleAsset::Status::ready)
            label (asset->status == SampleAsset::Status::loading ? "LOADING" : "PLAYABLE, ANALYSING", colour::arpOnDark);
        else if (type == "src.rhythm" && asset->tempoBpm > 0.0)
        {
            g.setColour (words.brighter (0.4f));
            g.drawText (juce::String (asset->tempoBpm, 1) + " BPM", inner, juce::Justification::centredRight, false);
        }
        return;
    }
    if (type == "proc.morph" || type == "proc.morph.global")
    {
        // The two sounds' bands, side by side: A in the first layer's light, B in the second's.
        const float w = inner.getWidth() / 14.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float a = juce::jlimit (0.0f, 1.0f, (tel (live, i) + 80.0f) / 80.0f);
            const float b = juce::jlimit (0.0f, 1.0f, (tel (live, 7 + i) + 80.0f) / 80.0f);
            g.setColour (colour::identity (0).wave);
            g.fillRect (inner.getX() + w * static_cast<float> (2 * i), inner.getBottom() - inner.getHeight() * a, w - 1.0f, inner.getHeight() * a);
            g.setColour (colour::identity (1).wave);
            g.fillRect (inner.getX() + w * static_cast<float> (2 * i + 1), inner.getBottom() - inner.getHeight() * b, w - 1.0f, inner.getHeight() * b);
        }
        if (type == "proc.morph" && tel (live, 14) < 0.5f)
            label ("PLAY TWO SOURCES", words);
        return;
    }
    if (type == "spatial.satellite")
    {
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getCentreY() - 0.5f, inner.getWidth(), 1.0f));
        g.setColour (lit);
        const float x = inner.getCentreX() + inner.getWidth() * 0.5f * tel (live, 0);
        g.fillEllipse (x - 3.5f, inner.getCentreY() - 3.5f, 7.0f, 7.0f);
        return;
    }
    if (type == "osc.analog")
    {
        label (live != nullptr && tel (live, 0) > 0.0f ? juce::String (tel (live, 0), 1) + " Hz" : juce::String ("PLAY A NOTE"), tel (live, 0) > 0.0f ? lit : words);
        return;
    }
    if (type == "util.voicemix")
    {
        label (juce::String (static_cast<int> (tel (live, 0))) + " VOICES", lit);
        return;
    }
    if (type == "src.external")
    {
        label (s.externals().status (nodeId).toUpperCase(), words);
        return;
    }
    // default: a level (the output's peak) or a control's value
    float level = live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
    if (type == "ctl.lfo" || type == "ctl.random" || type == "ctl.env" || type == "ctl.follower" || type == "proc.vca")
        level = std::abs (tel (live, 0));
    if (live != nullptr)
        const_cast<CompiledNode*> (live)->peak.store (level * 0.8f, std::memory_order_relaxed);   // decays between frames
    const auto bar = inner.withHeight (3.0f).withCentre (inner.getCentre());
    g.setColour (dim);
    g.fillRoundedRectangle (bar, 1.5f);
    g.setColour (level > 0.95f ? juce::Colour (0xffe2684a) : lit);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 1.5f);
}

void NodeView::mouseDown (const juce::MouseEvent& e)
{
    const int p = portAt (e.position);
    if (p >= 0 && ! e.mods.isPopupMenu())
    {
        canvas.portPressed (*this, p, e);
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (canvas.selection().count (nodeId) == 0)
            canvas.select (nodeId, false);
        if (p >= 0)
        {
            // Quick Add from a port: compatible modules only.
            QuickAddContext ctx;
            ctx.kind = QuickAddContext::Kind::port;
            ctx.node = group ? portList[static_cast<std::size_t> (p)].innerNode : nodeId;
            ctx.port = portList[static_cast<std::size_t> (p)].id;
            ctx.portIsOutput = ! portList[static_cast<std::size_t> (p)].input;
            ctx.world = canvas.toWorld (canvas.getLocalPoint (this, e.position)) + juce::Point<float> (ctx.portIsOutput ? 80.0f : -260.0f, 0.0f);
            if (canvas.onQuickAdd)
                canvas.onQuickAdd (ctx, e.getScreenPosition());
            return;
        }
        canvas.showNodeMenu (*this);
        return;
    }
    if (muteArea.contains (e.getPosition()) || soloArea.contains (e.getPosition()))
    {
        auto& s = canvas.session();
        if (muteArea.contains (e.getPosition()))
        {
            const auto* nm = s.document().findNode (nodeId);
            const auto* gm = s.document().findGroup (nodeId);
            s.setMuted (nodeId, ! (nm != nullptr ? nm->muted : (gm != nullptr && gm->muted)));
        }
        else
            s.toggleSolo (nodeId, e.mods.isShiftDown());
        return;
    }
    if (ledArea.contains (e.getPosition()) && ! group)
    {
        // The light: on / bypassed. A cross-fade in the running graph (Session::setEnabled).
        auto& s = canvas.session();
        if (const auto* n = s.document().findNode (nodeId))
            s.setEnabled (nodeId, ! n->enabled);
        return;
    }
    canvas.nodePressed (*this, e);
}

void NodeView::mouseDrag (const juce::MouseEvent& e) { canvas.nodeDragged (*this, e); }
void NodeView::mouseUp (const juce::MouseEvent& e) { canvas.nodeReleased (*this, e); }

void NodeView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (group)
        canvas.enterGroup (nodeId);
    else if (canvas.onOpenDetail)
        canvas.onOpenDetail (nodeId);
}

void NodeView::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    repaint();
}

void NodeView::mouseMove (const juce::MouseEvent& e)
{
    const int key = muteArea.contains (e.getPosition()) ? 1 : (soloArea.contains (e.getPosition()) ? 2 : 0);
    if (key != hoverKey)
    {
        hoverKey = key;
        if (key != 0)
            setTooltip (key == 1 ? "Mute (it keeps running; mute wins over solo)" : "Solo: only this sounds. Shift-click adds or removes it; click again to end the solo.");
        repaint (header);
    }
    if (key != 0)
        return;
    const int p = portAt (e.position);
    if (p != hoverPort)
    {
        hoverPort = p;
        if (p >= 0)
        {
            const auto& port = portList[static_cast<std::size_t> (p)];
            setTooltip (juce::String (port.name) + ": " + portKindName (port.kind) + (port.input ? " in" : " out")
                        + "\nDrag to connect; right-click for compatible modules.");
        }
        else if (ledArea.contains (e.getPosition()) && ! group)
            setTooltip ("On / bypass");
        else
            setTooltip (group ? "Double-click to open the group" : "Drag to move; double-click for every control; right-click for more");
        repaint();
    }
}

void NodeView::mouseExit (const juce::MouseEvent&)
{
    hoverPort = -1;
    hoverKey = 0;
    hover = false;
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> NodeView::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

//==============================================================================
Canvas::Canvas (Session& s) : model (s)
{
    setWantsKeyboardFocus (true);
    setTitle ("Patch board");
    setOpaque (true);   // (the board's own face: what moves on it never redraws the housing below)
    zoom = static_cast<float> (model.document().view.zoom);
    pan = { static_cast<float> (model.document().view.panX), static_cast<float> (model.document().view.panY) };
    rebuild();
    startTimerHz (25);
}

Canvas::~Canvas() = default;

juce::Point<float> Canvas::toWorld (juce::Point<float> p) const noexcept { return p / zoom + pan; }
juce::Point<float> Canvas::toCanvas (juce::Point<float> w) const noexcept { return (w - pan) * zoom; }

void Canvas::setBoardArea (juce::Rectangle<float> boardInCanvas)
{
    boardArea = boardInCanvas;
    repaint();
}

int Canvas::sourceOrdinal (const std::string& id) const
{
    int ordinal = 0;
    for (const auto& n : model.document().nodes)
    {
        const auto* d = model.registry().find (n.type);
        if (d == nullptr || d->role != NodeRole::source)
            continue;
        if (n.id == id)
            return ordinal;
        ++ordinal;
    }
    return -1;
}

std::string Canvas::visibleOwner (const std::string& node) const
{
    const auto& doc = model.document();
    const auto* n = doc.findNode (node);
    if (n == nullptr)
        return {};
    const auto current = doc.view.currentGroup;
    if (n->group == current)
        return node;
    // climb: the child group of the current view that contains it
    std::string g = n->group;
    for (int guard = 0; guard < 8 && ! g.empty(); ++guard)
    {
        const auto* gm = doc.findGroup (g);
        if (gm == nullptr)
            return {};
        if (gm->parent == current)
            return g;
        g = gm->parent;
    }
    return {};
}

NodeView* Canvas::viewFor (const std::string& id) const
{
    for (const auto& v : views)
        if (v->id() == id)
            return v.get();
    return nullptr;
}

void Canvas::rebuild()
{
    const auto& doc = model.document();
    std::vector<std::pair<std::string, bool>> wanted;
    for (const auto& g : doc.groups)
        if (g.parent == doc.view.currentGroup)
            wanted.emplace_back (g.id, true);
    for (const auto& n : doc.nodes)
        if (n.group == doc.view.currentGroup)
            wanted.emplace_back (n.id, false);
    views.erase (std::remove_if (views.begin(), views.end(), [&] (const auto& v) {
                     return std::none_of (wanted.begin(), wanted.end(), [&] (const auto& w) { return w.first == v->id() && w.second == v->isGroup(); });
                 }),
                 views.end());
    for (const auto& [id, isGroup] : wanted)
    {
        if (auto* v = viewFor (id))
            v->rebuild();
        else
        {
            views.push_back (std::make_unique<NodeView> (*this, id, isGroup));
            addAndMakeVisible (*views.back());
        }
    }
    for (auto it = selected.begin(); it != selected.end();)
        it = viewFor (*it) == nullptr ? selected.erase (it) : std::next (it);
    updateNodeTransforms();
    repaint();
}

void Canvas::refreshValues()
{
    for (auto& v : views)
    {
        v->refreshControls();
        v->repaint();   // (its light, its M and S: a tile's face is kept as an image)
    }
    repaint();
}

void Canvas::updateNodeTransforms()
{
    const auto& doc = model.document();
    for (auto& v : views)
    {
        juce::Point<float> world;
        if (v->isGroup())
        {
            const auto* g = doc.findGroup (v->id());
            world = g != nullptr ? juce::Point<float> (g->x, g->y) : juce::Point<float>();
        }
        else if (const auto* n = doc.findNode (v->id()))
            world = { n->x, n->y };
        // (a module's position is its face's corner: the room around it hangs outside)
        const auto topLeft = world - pan - juce::Point<float> (static_cast<float> (tile::pad), static_cast<float> (tile::pad));
        v->setBounds (juce::roundToInt (topLeft.x), juce::roundToInt (topLeft.y), v->getWidth(), v->getHeight());
        v->setTransform (juce::AffineTransform::scale (zoom));
    }
    repaint();
}

void Canvas::resized() { updateNodeTransforms(); }

bool Canvas::endPoint (const std::string& node, const std::string& port, bool input, juce::Point<float>& out) const
{
    const auto owner = visibleOwner (node);
    if (owner.empty())
    {
        // outside the group being edited: the rail at the board's edge
        out = { input ? static_cast<float> (getWidth()) - 6.0f : 6.0f, 60.0f + static_cast<float> (std::hash<std::string> {} (node) % 300) };
        return true;
    }
    auto* v = viewFor (owner);
    if (v == nullptr)
        return false;
    auto local = v->portPosition (node, port, input);
    if (! v->isGroup())
    {
        // a parameter target: the knob if it is on the tile, else the top of its face
        const auto* n = model.document().findNode (node);
        const auto* d = n != nullptr ? model.registry().find (n->type) : nullptr;
        if (input && d != nullptr && d->inputIndex (port) < 0 && d->paramIndex (port) >= 0)
        {
            local = { static_cast<float> (v->getWidth()) * 0.5f, static_cast<float> (tile::pad) };
            for (auto* c : v->getChildren())
                if (auto* k = dynamic_cast<ParamKnob*> (c); k != nullptr && k->paramId() == port)
                    local = k->getBounds().toFloat().getCentre().withY (static_cast<float> (k->getY()) + 2.0f);
        }
    }
    out = getLocalPoint (v, local);
    return true;
}

juce::Path Canvas::cablePath (juce::Point<float> a, juce::Point<float> b) const
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dx = std::max (40.0f * zoom, std::abs (b.x - a.x) * 0.5f);
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    return p;
}

void Canvas::forEachCable (const std::function<void (const Connection&, const juce::Path&, PortKind, bool hot, juce::Point<float> a, juce::Point<float> b)>& visit) const
{
    const auto& doc = model.document();
    for (const auto& c : doc.connections)
    {
        juce::Point<float> a, b;
        const auto ownerA = visibleOwner (c.from), ownerB = visibleOwner (c.to);
        if (ownerA.empty() && ownerB.empty())
            continue;
        if (ownerA == ownerB && viewFor (ownerA) != nullptr && viewFor (ownerA)->isGroup())
            continue;   // inside a collapsed group
        if (! endPoint (c.from, c.fromPort, false, a) || ! endPoint (c.to, c.toPort, true, b))
            continue;
        const auto* from = doc.findNode (c.from);
        const auto* fd = from != nullptr ? model.registry().find (from->type) : nullptr;
        PortKind kind = PortKind::audioMono;
        if (fd != nullptr && fd->outputIndex (c.fromPort) >= 0)
            kind = effectiveKind (fd->outputs[static_cast<std::size_t> (fd->outputIndex (c.fromPort))], from->domain);
        // (a modulation lands on its knob from above)
        juce::Path path;
        if (c.toParam)
        {
            path.startNewSubPath (a);
            const float dx = std::max (40.0f * zoom, std::abs (b.x - a.x) * 0.4f);
            path.cubicTo (a.x + dx, a.y, b.x, b.y - std::max (36.0f * zoom, std::abs (b.y - a.y) * 0.4f), b.x, b.y);
        }
        else
            path = cablePath (a, b);
        const bool hot = c.id == hoverCable || selected.count (ownerA) > 0 || selected.count (ownerB) > 0;
        visit (c, path, kind, hot, a, b);
    }
}

void Canvas::paintCables (juce::Graphics& g)
{
    // The routing board's wires: thin, warm grey, an arrowhead where the signal arrives. What
    // a wire carries shows in its ink (notes in the arpeggiator's amber, control in sage) and
    // its weight (stereo heavier); a modulation is dashed.
    forEachCable ([&] (const Connection& c, const juce::Path& path, PortKind kind, bool hot, juce::Point<float>, juce::Point<float> b) {
        juce::Colour ink = board::wire;
        if (kind == PortKind::notes)
            ink = colour::arp;
        else if (isControlLike (kind))
            ink = board::wireControl;
        if (hot)
            ink = c.id == hoverCable ? colour::accent : ink.darker (0.35f);
        const float width = (kind == PortKind::audioStereo ? 2.4f : 1.6f) * std::max (0.75f, std::sqrt (zoom));
        g.setColour (ink);
        if (c.toParam)
        {
            juce::Path dashed;
            const float dash[] = { 5.0f, 4.0f };
            juce::PathStrokeType (width).createDashedStroke (dashed, path, dash, 2);
            g.fillPath (dashed);
        }
        else
            g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // The arrowhead just before the socket (the modulation: pointing down onto its knob).
        const float size = 5.0f * std::max (0.75f, std::sqrt (zoom));
        const float socket = 7.0f * zoom;
        g.setColour (hot ? ink : board::arrow.interpolatedWith (ink, 0.5f));
        if (c.toParam)
            arrowAt (g, b.translated (0.0f, -1.0f), juce::MathConstants<float>::halfPi, size);
        else
            arrowAt (g, b.translated (-socket, 0.0f), 0.0f, size);
    });
    // Where the signal is going on the wires of what is selected: a bead (only while the
    // source really sounds).
    for (const auto& dot : activity)
    {
        g.setColour (colour::accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
    }
}

void Canvas::paint (juce::Graphics& g)
{
    // The board's face (the editor paints the same gradient around this component).
    const auto b = boardArea.isEmpty() ? getLocalBounds().toFloat() : boardArea;
    g.setGradientFill (juce::ColourGradient (board::faceTop, b.getX(), b.getY(), board::faceBottom, b.getRight(), b.getBottom(), false));
    g.fillAll();
    // A quiet grid of points (every 32 world px).
    const float step = 32.0f * zoom;
    if (step > 11.0f)
    {
        g.setColour (board::column.withAlpha (0.8f));
        const float ox = std::fmod (-pan.x * zoom, step), oy = std::fmod (-pan.y * zoom, step);
        const auto clip = g.getClipBounds().toFloat();
        for (float x = ox + step * std::floor ((clip.getX() - ox) / step); x < clip.getRight(); x += step)
            for (float y = oy + step * std::floor ((clip.getY() - oy) / step); y < clip.getBottom(); y += step)
                g.fillRect (x, y, 1.2f, 1.2f);
    }
    paintCables (g);
    if (drag == Drag::cable)
    {
        juce::Point<float> a;
        if (endPoint (cable.node, cable.port, ! cable.fromOutput, a))
        {
            const auto p = cable.fromOutput ? cablePath (a, dragNow) : cablePath (dragNow, a);
            g.setColour (colour::accent);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            drawSocket (g, dragNow, cable.kind, true, true);
        }
    }
    if (drag == Drag::rubber)
    {
        const auto r = juce::Rectangle<float> (dragStart, dragNow);
        g.setColour (board::sage.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (board::sage);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    // Inside a group: the way back, as a terminal on the board.
    const auto& doc = model.document();
    breadcrumbArea = {};
    if (! doc.view.currentGroup.empty())
    {
        juce::String crumb = "PATCH";
        std::vector<std::string> chain;
        for (auto gid = doc.view.currentGroup; ! gid.empty() && chain.size() < 8;)
        {
            chain.insert (chain.begin(), gid);
            const auto* gm = doc.findGroup (gid);
            gid = gm != nullptr ? gm->parent : std::string();
        }
        for (const auto& gid : chain)
            crumb << "   /   " << juce::String (doc.findGroup (gid)->name).toUpperCase();
        const auto font = fonts::make (11.5f, Weight::medium, 0.1f);
        breadcrumbArea = juce::Rectangle<float> (14.0f, 12.0f, 40.0f + juce::GlyphArrangement::getStringWidth (font, crumb), 26.0f);
        g.setColour (board::terminalFace);
        g.fillRoundedRectangle (breadcrumbArea, 6.0f);
        g.setColour (board::terminalEdge);
        g.drawRoundedRectangle (breadcrumbArea.reduced (0.5f), 6.0f, 1.0f);
        icon::draw (g, icon::Kind::previous, breadcrumbArea.withWidth (26.0f).reduced (4.0f), board::terminalInk, 1.4f);
        g.setColour (board::terminalInk);
        g.setFont (font);
        g.drawText (crumb, breadcrumbArea.withTrimmedLeft (26.0f), juce::Justification::centredLeft, false);
    }
    if (doc.nodes.empty())
    {
        g.setColour (board::soft);
        g.setFont (fonts::make (17.0f, Weight::regular, 0.01f));
        g.drawText ("Right-click the board, or press + ADD MODULE, and type \"analog\" to start.", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
        g.setFont (type::micro (13.5f));
        g.setColour (board::weak);
        g.drawText ("Drop a WAV, AIFF or FLAC here to play a recording.", getLocalBounds().withTrimmedTop (36), juce::Justification::centred);
    }
}

std::string Canvas::cableAt (juce::Point<float> p) const
{
    std::string best;
    float bestDistance = 7.0f;
    forEachCable ([&] (const Connection& c, const juce::Path& path, PortKind, bool, juce::Point<float>, juce::Point<float>) {
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        if (const float d = nearest.getDistanceFrom (p); d < bestDistance)
        {
            bestDistance = d;
            best = c.id;
        }
    });
    return best;
}

void Canvas::timerCallback()
{
    if (! isVisible())
        return;
    ++frame;
    if ((frame % 3) == 0)
        for (auto& v : views)
            v->refreshControls();
    for (auto& v : views)
        v->repaintLive();
    // The beads on the wires of what is selected or pointed at: only their own few pixels.
    auto dirty = [this] {
        for (const auto& dot : activity)
            repaint (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot).getSmallestIntegerContainer());
    };
    dirty();
    activity.clear();
    if (! hoverCable.empty() || ! selected.empty())
        forEachCable ([&] (const Connection& c, const juce::Path& path, PortKind kind, bool hot, juce::Point<float>, juce::Point<float>) {
            if (! hot)
                return;
            const auto* live = liveNode (model, c.from);
            const float level = live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
            if (level > 0.0005f || kind == PortKind::notes)
                activity.push_back (path.getPointAlongPath (path.getLength() * static_cast<float> ((frame % 50) / 50.0)));
        });
    dirty();
}

//==============================================================================
void Canvas::select (const std::string& id, bool add)
{
    if (! add)
        selected.clear();
    if (add && selected.count (id) > 0)
        selected.erase (id);
    else
        selected.insert (id);
    for (auto& v : views)
        v->repaint();
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void Canvas::clearSelection()
{
    selected.clear();
    for (auto& v : views)
        v->repaint();
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

std::string Canvas::primarySelection() const
{
    for (const auto& id : selected)
        if (model.document().findNode (id) != nullptr)
            return id;
    return {};
}

void Canvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStart = dragNow = e.position;
    moved = false;
    if (breadcrumbArea.contains (e.position))
    {
        breadcrumbClick (e.position);
        return;
    }
    const auto c = cableAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (! c.empty())
            showCableMenu (c, e.getScreenPosition());
        else if (onQuickAdd)
        {
            QuickAddContext ctx;
            ctx.kind = QuickAddContext::Kind::canvas;
            ctx.world = toWorld (e.position);
            onQuickAdd (ctx, e.getScreenPosition());
        }
        return;
    }
    if (e.mods.isMiddleButtonDown() || e.mods.isAltDown() || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
    {
        drag = Drag::pan;
        panAtStart = pan;
        return;
    }
    if (! c.empty())
    {
        hoverCable = c;
        repaint();
        return;
    }
    drag = Drag::rubber;
    if (! e.mods.isShiftDown())
        clearSelection();
}

void Canvas::mouseDrag (const juce::MouseEvent& e)
{
    dragNow = e.position;
    if (drag == Drag::pan)
    {
        pan = panAtStart - (e.position - dragStart) / zoom;
        updateNodeTransforms();
    }
    else if (drag == Drag::rubber || drag == Drag::cable)
        repaint();
}

void Canvas::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::rubber)
    {
        const auto r = juce::Rectangle<float> (dragStart, e.position);
        if (r.getWidth() > 4.0f || r.getHeight() > 4.0f)
        {
            for (auto& v : views)
                if (r.intersects (v->getBoundsInParent().toFloat()))
                    selected.insert (v->id());
            if (onSelectionChanged)
                onSelectionChanged();
        }
    }
    else if (drag == Drag::cable)
        finishCable (e.position);
    else if (drag == Drag::pan)
    {
        model.document().view.panX = pan.x;
        model.document().view.panY = pan.y;
    }
    drag = Drag::none;
    repaint();
}

void Canvas::mouseMove (const juce::MouseEvent& e)
{
    const auto c = cableAt (e.position);
    if (c != hoverCable)
    {
        hoverCable = c;
        if (const auto* con = model.document().findConnection (c))
        {
            const auto* a = model.document().findNode (con->from);
            const auto* b = model.document().findNode (con->to);
            juce::String tip;
            if (a != nullptr && b != nullptr)
            {
                tip << model.document().displayName (model.registry(), *a) << " " << con->fromPort << "  >  " << model.document().displayName (model.registry(), *b) << " "
                    << con->toPort;
                if (con->toParam)
                    tip << "\nmodulation " << juce::roundToInt (con->depth * 100.0) << " %";
                if (con->transpose != 0)
                    tip << "\ntranspose " << con->transpose << " st";
                if (const auto* live = liveNode (model, con->from); live != nullptr && live->latency > 0)
                    tip << "\nlatency " << live->latency << " samples";
                tip << "\nRight-click: insert a module, change, disconnect.";
            }
            setTooltip (tip);
        }
        else
            setTooltip ({});
        repaint();
    }
}

void Canvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const auto before = toWorld (e.position);
        zoom = juce::jlimit (0.3f, 2.5f, zoom * (1.0f + w.deltaY * 0.6f));
        pan = before - e.position / zoom;
    }
    else
        pan -= juce::Point<float> (w.deltaX, w.deltaY) * (360.0f / zoom) * (w.isReversed ? -1.0f : 1.0f);
    model.document().view.zoom = zoom;
    model.document().view.panX = pan.x;
    model.document().view.panY = pan.y;
    updateNodeTransforms();
}

void Canvas::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    const auto before = toWorld (e.position);
    zoom = juce::jlimit (0.3f, 2.5f, zoom * scale);
    pan = before - e.position / zoom;
    model.document().view.zoom = zoom;
    updateNodeTransforms();
}

void Canvas::moveViewBy (juce::Point<float> d)
{
    pan += d;
    updateNodeTransforms();
}

void Canvas::fitAll()
{
    if (views.empty())
    {
        resetZoom();
        return;
    }
    juce::Rectangle<float> box;
    bool first = true;
    for (auto& v : views)
    {
        const auto* n = model.document().findNode (v->id());
        const auto* gm = model.document().findGroup (v->id());
        const juce::Point<float> at = n != nullptr ? juce::Point<float> (n->x, n->y) : (gm != nullptr ? juce::Point<float> (gm->x, gm->y) : juce::Point<float>());
        const auto r = juce::Rectangle<float> (at.x, at.y, static_cast<float> (v->getWidth() - 2 * tile::pad), static_cast<float> (v->getHeight() - 2 * tile::pad));
        box = first ? r : box.getUnion (r);
        first = false;
    }
    box = box.expanded (44.0f, 36.0f);
    // Readable first (60 .. 110 %): a patch wider than the board is centred and panned, not shrunk.
    zoom = juce::jlimit (0.6f, 1.1f, std::min (static_cast<float> (getWidth()) / box.getWidth(), static_cast<float> (getHeight()) / box.getHeight()));
    pan = box.getCentre() - juce::Point<float> (static_cast<float> (getWidth()), static_cast<float> (getHeight())) * 0.5f / zoom;
    model.document().view.zoom = zoom;
    model.document().view.panX = pan.x;
    model.document().view.panY = pan.y;
    updateNodeTransforms();
}

void Canvas::resetZoom()
{
    zoom = 1.0f;
    model.document().view.zoom = 1.0;
    updateNodeTransforms();
}

//==============================================================================
void Canvas::nodePressed (NodeView& v, const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
        select (v.id(), true);
    else if (selected.count (v.id()) == 0)
        select (v.id(), false);
    drag = Drag::move;
    moved = false;
    dragStart = getLocalPoint (&v, e.position);
    startPositions.clear();
    const auto& doc = model.document();
    for (const auto& id : selected)
    {
        if (const auto* n = doc.findNode (id))
            startPositions[id] = { n->x, n->y };
        else if (const auto* g = doc.findGroup (id))
            startPositions[id] = { g->x, g->y };
    }
    v.toFront (false);
}

void Canvas::nodeDragged (NodeView& v, const juce::MouseEvent& e)
{
    if (drag == Drag::cable)
    {
        dragNow = getLocalPoint (&v, e.position);
        repaint();
        return;
    }
    if (drag != Drag::move)
        return;
    const auto now = getLocalPoint (&v, e.position);
    const auto delta = (now - dragStart) / zoom;
    if (delta.getDistanceFromOrigin() > 2.0f)
        moved = true;
    auto& doc = model.document();
    for (const auto& [id, start] : startPositions)
    {
        // snap to the 8 px grid
        const float x = std::round ((start.x + delta.x) / 8.0f) * 8.0f, y = std::round ((start.y + delta.y) / 8.0f) * 8.0f;
        if (auto* n = doc.findNode (id))
        {
            n->x = x;
            n->y = y;
        }
        else if (auto* g = doc.findGroup (id))
        {
            g->x = x;
            g->y = y;
        }
    }
    updateNodeTransforms();
}

void Canvas::nodeReleased (NodeView& v, const juce::MouseEvent& e)
{
    if (drag == Drag::cable)
    {
        finishCable (getLocalPoint (&v, e.position));
        drag = Drag::none;
        repaint();
        return;
    }
    if (drag != Drag::move)
        return;
    drag = Drag::none;
    if (! moved)
        return;
    // Record the move as one undoable step (positions restored, then set by the transaction).
    auto& doc = model.document();
    std::map<std::string, juce::Point<float>> finals;
    for (const auto& [id, start] : startPositions)
    {
        if (auto* n = doc.findNode (id))
        {
            finals[id] = { n->x, n->y };
            n->x = start.x;
            n->y = start.y;
        }
        else if (auto* g = doc.findGroup (id))
        {
            finals[id] = { g->x, g->y };
            g->x = start.x;
            g->y = start.y;
        }
    }
    // A source dropped on another source: LAYER / MORPH / MODULATE / REPLACE.
    NodeView* target = nullptr;
    if (startPositions.size() == 1 && v.isSource())
        for (auto& other : views)
            if (other.get() != &v && other->isSource() && other->getBoundsInParent().intersects (v.getBoundsInParent()))
                target = other.get();
    model.perform ("move", [&] (Document& d) {
        for (const auto& [id, at] : finals)
        {
            if (auto* n = d.findNode (id))
            {
                n->x = at.x;
                n->y = at.y;
            }
            else if (auto* g = d.findGroup (id))
            {
                g->x = at.x;
                g->y = at.y;
            }
        }
        edit::Result r;
        r.ok = true;
        return r;
    }, false);
    updateNodeTransforms();
    if (target != nullptr)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Drop on " + target->getTitle());
        const auto dragged = v.id(), onto = target->id();
        const auto backTo = startPositions.begin()->second;
        auto act = [this, dragged, onto, backTo] (edit::DropAction a, const char* label) {
            model.perform (label, [&] (Document& d) {
                if (auto* n = d.findNode (dragged); n != nullptr && a != edit::DropAction::replace)
                {
                    n->x = backTo.x;   // the dragged card returns beside its partner
                    n->y = backTo.y;
                }
                return edit::dropOnSource (d, model.registry(), dragged, onto, a);
            });
        };
        menu.addItem ("LAYER   both sound", [act] { act (edit::DropAction::layer, "layer"); });
        menu.addItem ("MORPH   fuse them into one sound", [act] { act (edit::DropAction::morph, "morph"); });
        menu.addItem ("MODULATE   frequency-modulate it", [act] { act (edit::DropAction::modulate, "modulate"); });
        menu.addItem ("REPLACE   take its place", [act] { act (edit::DropAction::replace, "replace"); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
    }
}

void Canvas::portPressed (NodeView& v, int port, const juce::MouseEvent& e)
{
    const auto& p = v.ports()[static_cast<std::size_t> (port)];
    cable.node = v.isGroup() ? p.innerNode : v.id();
    cable.port = p.id;
    cable.fromOutput = ! p.input;
    cable.kind = p.kind;
    drag = Drag::cable;
    dragNow = getLocalPoint (&v, e.position);
    repaint();
}

void Canvas::finishCable (juce::Point<float> at)
{
    // A port under the release point?
    for (auto& v : views)
    {
        const auto local = v->getLocalPoint (this, at);
        const int pi = v->portAt (local);
        if (pi < 0)
            continue;
        const auto& p = v->ports()[static_cast<std::size_t> (pi)];
        const auto other = v->isGroup() ? p.innerNode : v->id();
        if (p.input == ! cable.fromOutput)
        {
            const auto from = cable.fromOutput ? cable.node : other, fromPort = cable.fromOutput ? cable.port : p.id;
            const auto to = cable.fromOutput ? other : cable.node, toPort = cable.fromOutput ? p.id : cable.port;
            const bool isParam = v->isGroup() && model.registry().find (model.document().findNode (to)->type)->inputIndex (toPort) < 0;
            model.perform ("connect", [&] (Document& d) {
                return isParam ? edit::modulate (d, model.registry(), from, fromPort, to, toPort, 0.5) : edit::connectSmart (d, model.registry(), from, fromPort, to, toPort);
            });
        }
        return;
    }
    // A knob under it: modulation (from a control output).
    if (auto* c = getComponentAt (at.toInt()))
        for (auto* comp = c; comp != nullptr && comp != this; comp = comp->getParentComponent())
            if (auto* k = dynamic_cast<ParamKnob*> (comp))
            {
                if (cable.fromOutput && isControlLike (cable.kind))
                    model.perform ("modulate", [&] (Document& d) { return edit::modulate (d, model.registry(), cable.node, cable.port, k->nodeId(), k->paramId(), 0.3); });
                else
                    model.message ("only a control signal can modulate a knob", false, true);
                return;
            }
    // A module's body: its first compatible port.
    for (auto& v : views)
        if (v->getBoundsInParent().toFloat().contains (at) && ! v->isGroup() && v->id() != cable.node)
        {
            const auto* n = model.document().findNode (v->id());
            const auto* d = model.registry().find (n->type);
            if (d == nullptr)
                return;
            model.perform ("connect", [&] (Document& doc) {
                edit::Result last;
                last.message = "no compatible port on " + d->name;
                for (const auto& p : cable.fromOutput ? d->inputs : d->outputs)
                {
                    auto r = cable.fromOutput ? edit::connectSmart (doc, model.registry(), cable.node, cable.port, v->id(), p.id)
                                              : edit::connectSmart (doc, model.registry(), v->id(), p.id, cable.node, cable.port);
                    if (r.ok)
                        return r;
                    last = r;
                }
                return last;
            });
            return;
        }
    // Empty canvas: Quick Add, compatible modules only.
    if (onQuickAdd && at.getDistanceFrom (dragStart) > 20.0f)
    {
        QuickAddContext ctx;
        ctx.kind = QuickAddContext::Kind::port;
        ctx.node = cable.node;
        ctx.port = cable.port;
        ctx.portIsOutput = cable.fromOutput;
        ctx.world = toWorld (at);
        onQuickAdd (ctx, localPointToGlobal (at).toInt());
    }
}

void Canvas::showCableMenu (const std::string& id, juce::Point<int> screen)
{
    const auto* c = model.document().findConnection (id);
    if (c == nullptr)
        return;
    juce::PopupMenu menu;
    const bool isParam = c->toParam;
    if (! isParam)
        menu.addItem ("Insert a module here...", [this, id, screen] {
            if (onQuickAdd)
            {
                QuickAddContext ctx;
                ctx.kind = QuickAddContext::Kind::cable;
                ctx.connection = id;
                onQuickAdd (ctx, screen);
            }
        });
    if (isParam)
    {
        juce::PopupMenu depths;
        for (const int d : { -100, -50, -25, -10, 10, 25, 50, 100 })
            depths.addItem (juce::String (d) + " %", true, juce::roundToInt (c->depth * 100.0) == d, [this, id, d] {
                model.perform ("depth", [&] (Document& doc) {
                    edit::Result r;
                    if (auto* con = doc.findConnection (id))
                    {
                        con->depth = d / 100.0;
                        r.ok = true;
                    }
                    return r;
                }, false);
            });
        menu.addSubMenu ("Depth", depths);
    }
    const auto* from = model.document().findNode (c->from);
    const auto* fd = from != nullptr ? model.registry().find (from->type) : nullptr;
    if (fd != nullptr && fd->outputIndex (c->fromPort) >= 0 && fd->outputs[static_cast<std::size_t> (fd->outputIndex (c->fromPort))].kind == PortKind::notes)
    {
        juce::PopupMenu tr;
        for (const int t : { -24, -12, -7, -5, 0, 5, 7, 12, 24 })
            tr.addItem ((t > 0 ? "+" : "") + juce::String (t) + " st", true, c->transpose == t, [this, id, t] {
                model.perform ("transpose", [&] (Document& doc) {
                    edit::Result r;
                    if (auto* con = doc.findConnection (id))
                    {
                        con->transpose = t;
                        r.ok = true;
                    }
                    return r;
                });
            });
        menu.addSubMenu ("Transpose these notes", tr);
    }
    menu.addItem ("Disconnect", [this, id] {
        model.perform ("disconnect", [&] (Document& d) {
            d.disconnect (id);
            edit::Result r;
            r.ok = true;
            r.message = "disconnected";
            return r;
        });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screen.x, screen.y, 1, 1 }));
}

void Canvas::showNodeMenu (NodeView& v)
{
    const auto id = v.id();
    juce::PopupMenu menu;
    std::vector<std::string> ids (selected.begin(), selected.end());
    if (ids.empty())
        ids.push_back (id);
    if (v.isGroup())
    {
        menu.addItem ("Open group", [this, id] { enterGroup (id); });
        menu.addItem ("Save as module...", [this, id] {
            auto chooser = std::make_shared<juce::FileChooser> ("Save module", Session::userDirectory().getChildFile ("Modules"), "*.andormodule");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, id, chooser] (const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                f = f.withFileExtension ("andormodule");
                f.getParentDirectory().createDirectory();
                if (f.replaceWithText (juce::String (edit::exportModule (model.document(), id))))
                    model.message ("saved module " + f.getFileName());
            });
        });
        menu.addItem ("Ungroup", [this, id] { model.perform ("ungroup", [&] (Document& d) { return edit::ungroup (d, id); }); });
    }
    else
    {
        const auto* n = model.document().findNode (id);
        const auto* d = n != nullptr ? model.registry().find (n->type) : nullptr;
        menu.addItem ("Every control...", [this, id] {
            if (onOpenDetail)
                onOpenDetail (id);
        });
        menu.addItem (n != nullptr && n->enabled ? "Bypass" : "Enable", [this, id] {
            model.perform ("bypass", [&] (Document& doc) {
                edit::Result r;
                if (auto* x = doc.findNode (id))
                {
                    x->enabled = ! x->enabled;
                    r.ok = true;
                }
                return r;
            });
        });
        menu.addItem ("Rename...", [this, id] {
            auto* w = new juce::AlertWindow ("Rename", {}, juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", juce::String (model.document().displayName (model.registry(), *model.document().findNode (id))));
            w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, id, w] (int result) {
                                    if (result != 1)
                                        return;
                                    const auto name = w->getTextEditorContents ("name").toUpperCase().toStdString();
                                    model.perform ("rename", [&] (Document& doc) {
                                        edit::Result r;
                                        if (auto* x = doc.findNode (id))
                                        {
                                            x->label = name;
                                            r.ok = true;
                                        }
                                        return r;
                                    });
                                }),
                                true);
        });
        if (d != nullptr && d->supports (Domain::voice) && d->supports (Domain::global))
        {
            const bool isVoice = n->domain == Domain::voice;
            menu.addItem (isVoice ? "Run once (GLOBAL)" : "Run per voice (VOICE)", [this, id, isVoice] {
                model.perform ("domain", [&] (Document& doc) {
                    edit::Result r;
                    auto* x = doc.findNode (id);
                    x->domain = isVoice ? Domain::global : Domain::voice;
                    // cables that no longer fit are dropped (the message says how many)
                    Validator val (model.registry());
                    int dropped = 0;
                    const auto cons = doc.connections;
                    for (const auto& c : cons)
                        if ((c.from == id || c.to == id) && ! val.checkConnection (doc, c.from, c.fromPort, c.to, c.toPort, c.toParam, c.id).ok)
                        {
                            doc.disconnect (c.id);
                            ++dropped;
                        }
                    r.ok = true;
                    r.message = std::string (isVoice ? "now global" : "now per voice") + (dropped > 0 ? " (" + std::to_string (dropped) + " cables no longer fit)" : "");
                    return r;
                });
            });
        }
        menu.addItem ("Replace with...", [this, id, screenPoint = v.getScreenBounds().getCentre()] {
            if (onQuickAdd)
            {
                QuickAddContext ctx;
                ctx.kind = QuickAddContext::Kind::selection;
                ctx.node = id;
                ctx.port = "replace";
                onQuickAdd (ctx, screenPoint);
            }
        });
    }
    menu.addSeparator();
    menu.addItem ("Duplicate", [this, ids] { model.perform ("duplicate", [&] (Document& d) { return edit::duplicate (d, model.registry(), ids, false); }); });
    menu.addItem ("Duplicate with links", [this, ids] { model.perform ("duplicate", [&] (Document& d) { return edit::duplicate (d, model.registry(), ids, true); }); });
    menu.addItem ("Group selection", [this, ids] {
        model.perform ("group", [&] (Document& d) { return edit::group (d, model.registry(), ids, "GROUP"); });
    });
    menu.addItem ("Delete", [this, ids] {
        model.perform ("delete", [&] (Document& d) {
            for (const auto& x : ids)
            {
                if (d.findGroup (x) != nullptr)
                {
                    for (const auto& m : edit::groupMembers (d, x))
                        d.removeNode (m);
                    edit::ungroup (d, x);
                }
                else
                    d.removeNode (x);
            }
            edit::Result r;
            r.ok = true;
            r.message = "deleted " + std::to_string (ids.size());
            return r;
        });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&v));
}

void Canvas::enterGroup (const std::string& groupId)
{
    model.document().view.currentGroup = groupId;
    selected.clear();
    rebuild();
    fitAll();
}

void Canvas::breadcrumbClick (juce::Point<float>)
{
    auto& doc = model.document();
    const auto* g = doc.findGroup (doc.view.currentGroup);
    doc.view.currentGroup = g != nullptr ? g->parent : std::string();
    selected.clear();
    rebuild();
    fitAll();
}

bool Canvas::keyPressed (const juce::KeyPress& k)
{
    std::vector<std::string> ids (selected.begin(), selected.end());
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! ids.empty())
    {
        model.perform ("delete", [&] (Document& d) {
            for (const auto& x : ids)
                d.removeNode (x);
            edit::Result r;
            r.ok = true;
            r.message = "deleted " + std::to_string (ids.size());
            return r;
        });
        return true;
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'D' && ! ids.empty())
    {
        const bool links = k.getModifiers().isShiftDown();
        model.perform ("duplicate", [&] (Document& d) { return edit::duplicate (d, model.registry(), ids, links); });
        return true;
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'G' && ! ids.empty())
    {
        model.perform ("group", [&] (Document& d) { return edit::group (d, model.registry(), ids, "GROUP"); });
        return true;
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'A')
    {
        for (auto& v : views)
            selected.insert (v->id());
        repaint();
        if (onSelectionChanged)
            onSelectionChanged();
        return true;
    }
    if (k.getKeyCode() == 'F' && ! k.getModifiers().isAnyModifierKeyDown())
    {
        fitAll();
        return true;
    }
    if (k.getKeyCode() == '0' && ! k.getModifiers().isAnyModifierKeyDown())
    {
        resetZoom();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        if (! model.document().view.currentGroup.empty())
            breadcrumbClick ({});
        else
            clearSelection();
        return true;
    }
    if (k == juce::KeyPress::tabKey && ! views.empty())
    {
        // keyboard navigation between modules
        std::size_t next = 0;
        for (std::size_t i = 0; i < views.size(); ++i)
            if (selected.count (views[i]->id()) > 0)
                next = (i + (k.getModifiers().isShiftDown() ? views.size() - 1 : 1)) % views.size();
        select (views[next]->id(), false);
        return true;
    }
    if (k == juce::KeyPress::returnKey && ! ids.empty())
    {
        if (onOpenDetail)
            onOpenDetail (ids.front());
        return true;
    }
    return false;
}

bool Canvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
            || f.endsWithIgnoreCase (".andormodule") || f.endsWithIgnoreCase (".andormodular"))
            return true;
    return false;
}

void Canvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    const juce::File f (files[0]);
    const auto at = juce::Point<float> (static_cast<float> (x), static_cast<float> (y));
    const auto world = toWorld (at);
    if (f.hasFileExtension ("andormodular"))
    {
        model.loadFile (f);
        return;
    }
    if (f.hasFileExtension ("andormodule"))
    {
        const auto text = f.loadFileAsString().toStdString();
        model.perform ("add module", [&] (Document& d) { return edit::importModule (d, model.registry(), text, world.x, world.y); });
        return;
    }
    const auto key = model.keyFor (f);
    // Dropped on a module that plays recordings: (re)link it.
    for (auto& v : views)
        if (v->getBoundsInParent().toFloat().contains (at) && ! v->isGroup())
        {
            const auto* n = model.document().findNode (v->id());
            if (n != nullptr && n->strings.count ("sample") > 0)
            {
                model.setNodeString (v->id(), "sample", key);
                model.message ("now playing " + f.getFileName());
                return;
            }
        }
    juce::PopupMenu menu;
    menu.addSectionHeader (f.getFileName() + " as...");
    auto add = [this, key, world, name = f.getFileName()] (const char* type, Domain domain) {
        model.perform ("add recording", [&] (Document& d) {
            auto r = edit::addModule (d, model.registry(), type, world.x, world.y);
            if (r.ok)
                if (auto* n = d.findNode (r.focus))
                {
                    n->strings["sample"] = key;
                    if (n->domain != domain)
                        n->domain = domain;
                }
            return r;
        });
    };
    menu.addItem ("ORGANIC SAMPLE   play it across the keyboard", [add] { add ("src.organic", Domain::voice); });
    menu.addItem ("GRANULAR   grains of it per note", [add] { add ("src.granular", Domain::voice); });
    menu.addItem ("RHYTHMIC SOURCE   a loop on the host's beat", [add] { add ("src.rhythm", Domain::global); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (at).toInt())));
}

} // namespace andor::modular::ui
