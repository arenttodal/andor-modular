#include "Canvas.h"

#include <cmath>

namespace andor::modular::ui
{

namespace
{
    constexpr int knobW = 40, knobH = 50, choiceH = 22, telemetryH = 22;

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

    /** A group's own control on its card (moves the group control channel live). */
    class GroupKnob final : public juce::Slider
    {
    public:
        GroupKnob (Session& s, std::string g, int c) : session (s), group (std::move (g)), control (c)
        {
            setSliderStyle (RotaryHorizontalVerticalDrag);
            setTextBoxStyle (NoTextBox, true, 0, 0);
            setRange (0.0, 1.0);
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
            g.setColour (colour::boneDim);
            g.setFont (text (10.0f, 500));
            g.drawFittedText (gm != nullptr && control < static_cast<int> (gm->controls.size()) ? juce::String (gm->controls[static_cast<std::size_t> (control)].name) : juce::String(),
                              r.removeFromBottom (ParamKnob::labelHeight), juce::Justification::centred, 1, 0.8f);
            drawKnob (g, r.toFloat().reduced (2.0f), static_cast<float> (getValue()), false, 0.0f, false, isMouseOverOrDragging(), colour::ember);
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
                auto c = makeControl (s, nodeId, p);
                addAndMakeVisible (*c);
                controls.push_back (std::move (c));
            }
        }
    }
    const int rows = std::max (rowsIn, rowsOut);
    int knobCount = 0, choiceCount = 0;
    for (auto& c : controls)
        (dynamic_cast<juce::ComboBox*> (c.get()) != nullptr || dynamic_cast<juce::Button*> (c.get()) != nullptr ? choiceCount : knobCount)++;
    const int height = space::headerHeight + 6 + rows * space::portSpacing + (choiceCount > 0 ? choiceH + 6 : 0) + (knobCount > 0 ? knobH + 4 : 0) + telemetryH + 8;
    setSize (space::nodeWidth, height);
    int yi = space::headerHeight + 6 + space::portSpacing / 2, yo = yi;
    for (auto& p : portList)
    {
        if (p.input)
        {
            p.at = { 0.0f, static_cast<float> (yi) };
            yi += space::portSpacing;
        }
        else
        {
            p.at = { static_cast<float> (space::nodeWidth), static_cast<float> (yo) };
            yo += space::portSpacing;
        }
    }
    resized();
    const auto* n = doc.findNode (nodeId);
    setTitle (group ? juce::String (doc.findGroup (nodeId)->name) : (n != nullptr ? juce::String (doc.displayName (s.registry(), *n)) : juce::String()));
}

void NodeView::resized()
{
    int rows = 0;
    for (const auto& p : portList)
        rows = std::max (rows, p.input ? 1 : 0);
    int rowsIn = 0, rowsOut = 0;
    for (const auto& p : portList)
        (p.input ? rowsIn : rowsOut)++;
    header = getLocalBounds().removeFromTop (space::headerHeight);
    ledArea = header.removeFromRight (26);
    auto area = getLocalBounds().withTrimmedTop (space::headerHeight + 6 + std::max (rowsIn, rowsOut) * space::portSpacing).reduced (8, 0);
    std::vector<juce::Component*> knobs, choices;
    for (auto& c : controls)
        (dynamic_cast<juce::ComboBox*> (c.get()) != nullptr || dynamic_cast<juce::Button*> (c.get()) != nullptr ? choices : knobs).push_back (c.get());
    for (auto* c : choices)
    {
        c->setBounds (area.removeFromTop (choiceH));
        area.removeFromTop (6);
    }
    if (! knobs.empty())
    {
        auto row = area.removeFromTop (knobH);
        const int w = row.getWidth() / std::max<int> (4, static_cast<int> (knobs.size()));
        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (std::min (w, knobW + 4), knobH));
        area.removeFromTop (4);
    }
    telemetryArea = area.removeFromTop (telemetryH);
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

int NodeView::portAt (juce::Point<float> local) const
{
    for (std::size_t i = 0; i < portList.size(); ++i)
        if (portList[i].at.getDistanceFrom (local) < 10.0f)
            return static_cast<int> (i);
    return -1;
}

juce::Point<float> NodeView::portPosition (const std::string& node, const std::string& port, bool input) const
{
    for (const auto& p : portList)
        if (p.input == input && p.id == port && (! group || p.innerNode == node))
            return p.at;
    // a group card without that port exposed: its edge
    return { input ? 0.0f : static_cast<float> (getWidth()), static_cast<float> (space::headerHeight / 2) };
}

void NodeView::paint (juce::Graphics& g)
{
    auto& s = canvas.session();
    const auto& doc = s.document();
    const bool sel = canvas.selection().count (nodeId) > 0;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto* n = group ? nullptr : doc.findNode (nodeId);
    const auto* d = n != nullptr ? s.registry().find (n->type) : nullptr;
    const bool enabled = group || (n != nullptr && n->enabled);
    const bool missing = ! group && d == nullptr;

    // the card
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), static_cast<float> (space::corner));
    g.setColour (group ? colour::card.brighter (0.06f) : colour::card);
    g.fillRoundedRectangle (r, static_cast<float> (space::corner));
    g.setColour (colour::cardHigh);
    g.fillRoundedRectangle (r.withHeight (static_cast<float> (space::headerHeight)), static_cast<float> (space::corner));
    g.fillRect (r.withTop (static_cast<float> (space::headerHeight) - 6.0f).withHeight (6.0f));
    g.setColour (sel ? colour::ember : (missing ? colour::error : colour::line));
    g.drawRoundedRectangle (r, static_cast<float> (space::corner), sel ? 1.6f : 1.0f);
    if (group)
    {
        // a stacked edge: a group holds several modules
        g.setColour (colour::line);
        g.drawRoundedRectangle (r.translated (3.0f, -3.0f).withTrimmedRight (3.0f).withHeight (8.0f), 3.0f, 1.0f);
    }

    // header: category tick, name, domain chip, power LED
    juce::String title, chip;
    juce::Colour tick = colour::sporeDim;
    if (group)
    {
        const auto* gm = doc.findGroup (nodeId);
        title = gm != nullptr ? juce::String (gm->name) : juce::String ("GROUP");
        chip = juce::String (static_cast<int> (edit::groupMembers (doc, nodeId).size())) + " MOD";
        tick = colour::ember;
    }
    else if (n != nullptr)
    {
        title = juce::String (doc.displayName (s.registry(), *n));
        chip = n->domain == Domain::voice ? "VOICE" : (n->domain == Domain::event ? "NOTES" : "GLOBAL");
        if (d != nullptr)
            tick = d->category == "Sources" ? colour::ember : (d->category == "Controllers" ? colour::port (PortKind::control)
                                                                : (d->category == "Spatial" ? colour::port (PortKind::gate) : colour::boneDim));
    }
    g.setColour (tick);
    g.fillRect (juce::Rectangle<float> (8.0f, 9.0f, 3.0f, 10.0f));
    g.setColour (enabled ? colour::bone : colour::boneFaint);
    g.setFont (text (12.0f, 600));
    auto titleArea = header.withTrimmedLeft (16).withTrimmedRight (52);
    g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 1, 0.75f);
    g.setFont (text (8.5f, 600));
    const auto chipArea = juce::Rectangle<int> (header.getRight() - 50, 8, 46, 13);
    g.setColour (colour::line);
    g.fillRoundedRectangle (chipArea.toFloat(), 3.0f);
    g.setColour (colour::boneDim);
    g.drawText (chip, chipArea, juce::Justification::centred);
    g.setColour (enabled ? colour::ember : colour::sporeDim);
    g.fillEllipse (ledArea.toFloat().withSizeKeepingCentre (7.0f, 7.0f));

    // ports and their names
    std::set<std::string> connectedIn, connectedOut;
    for (const auto& c : doc.connections)
    {
        if (c.to == nodeId || (group && std::any_of (portList.begin(), portList.end(), [&] (const Port& p) { return p.innerNode == c.to && p.id == c.toPort; })))
            connectedIn.insert ((group ? c.to + "/" : std::string()) + c.toPort);
        if (c.from == nodeId || (group && std::any_of (portList.begin(), portList.end(), [&] (const Port& p) { return p.innerNode == c.from && p.id == c.fromPort; })))
            connectedOut.insert ((group ? c.from + "/" : std::string()) + c.fromPort);
    }
    g.setFont (text (10.0f, 500));
    for (std::size_t i = 0; i < portList.size(); ++i)
    {
        const auto& p = portList[i];
        const auto key = (group ? p.innerNode + "/" : std::string()) + p.id;
        const bool on = p.input ? connectedIn.count (key) > 0 : connectedOut.count (key) > 0;
        drawPort (g, p.at, p.kind, on, static_cast<int> (i) == hoverPort);
        g.setColour (on ? colour::bone : colour::boneDim);
        const auto labelArea = p.input ? juce::Rectangle<float> (12.0f, p.at.y - 8.0f, 80.0f, 16.0f) : juce::Rectangle<float> (static_cast<float> (getWidth()) - 92.0f, p.at.y - 8.0f, 80.0f, 16.0f);
        g.drawText (p.name, labelArea, p.input ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }
    if (missing)
    {
        g.setColour (colour::error);
        g.setFont (text (10.5f));
        g.drawFittedText ("missing module: " + juce::String (n != nullptr ? n->type : std::string()) + " (kept, silent)", getLocalBounds().withTrimmedTop (space::headerHeight).reduced (10, 4),
                          juce::Justification::topLeft, 3);
    }
    paintTelemetry (g, telemetryArea.toFloat());
}

void NodeView::paintTelemetry (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto& s = canvas.session();
    if (group || area.isEmpty())
        return;
    const auto* n = s.document().findNode (nodeId);
    if (n == nullptr)
        return;
    const auto* live = liveNode (s, nodeId);
    g.setColour (colour::peatDeep);
    g.fillRoundedRectangle (area, 3.0f);
    auto inner = area.reduced (6.0f, 4.0f);
    g.setFont (text (9.5f, 500));
    const auto& type = n->type;
    auto label = [&] (const juce::String& t, juce::Colour c = colour::boneDim) {
        g.setColour (c);
        g.drawText (t, inner, juce::Justification::centredLeft);
    };
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
            g.setColour (now ? colour::ember : colour::sporeDim);
            g.fillRect (cell);
        }
        return;
    }
    if (type == "src.granular")
    {
        g.setColour (colour::sporeDim);
        g.drawHorizontalLine (static_cast<int> (inner.getCentreY()), inner.getX(), inner.getRight());
        const int count = live != nullptr ? live->telemetryCount.load() - 2 : 0;
        g.setColour (colour::spore);
        for (int i = 0; i < count; ++i)
            g.fillEllipse (inner.getX() + inner.getWidth() * tel (live, 2 + i) - 2.0f, inner.getY() + 2.0f + static_cast<float> ((i * 5) % 9), 4.0f, 4.0f);
        g.setColour (colour::ember);
        g.drawVerticalLine (static_cast<int> (inner.getX() + inner.getWidth() * tel (live, 1)), inner.getY(), inner.getBottom());
        g.setColour (colour::boneDim);
        g.drawText (juce::String (static_cast<int> (tel (live, 0))) + " GRAINS", inner, juce::Justification::centredRight);
        return;
    }
    if (type == "src.organic" || type == "src.rhythm")
    {
        const auto it = n->strings.find ("sample");
        const auto key = it != n->strings.end() ? it->second : std::string();
        const auto asset = key.empty() ? nullptr : s.assets().resolve (s.resolveKey (key).getFullPathName().toStdString());
        if (asset == nullptr || key.empty())
        {
            label ("DROP A RECORDING", colour::warn);
            return;
        }
        if (asset->status == SampleAsset::Status::missing || asset->status == SampleAsset::Status::failed)
        {
            label (asset->status == SampleAsset::Status::missing ? "MISSING: DROP TO RELINK" : "UNREADABLE", colour::error);
            return;
        }
        if (! asset->overview.empty())
        {
            g.setColour (colour::sporeDim);
            const float w = inner.getWidth() / static_cast<float> (asset->overview.size());
            for (std::size_t i = 0; i < asset->overview.size(); ++i)
            {
                const float h = std::max (1.0f, inner.getHeight() * asset->overview[i]);
                g.fillRect (inner.getX() + w * static_cast<float> (i), inner.getCentreY() - h * 0.5f, std::max (1.0f, w - 0.5f), h);
            }
        }
        if (type == "src.organic" && tel (live, 0) >= 0.0f && live != nullptr && live->telemetryCount.load() > 0)
        {
            g.setColour (colour::ember);
            g.drawVerticalLine (static_cast<int> (inner.getX() + inner.getWidth() * tel (live, 0)), inner.getY(), inner.getBottom());
        }
        if (asset->status != SampleAsset::Status::ready)
            label (asset->status == SampleAsset::Status::loading ? "LOADING" : "PLAYABLE, ANALYSING", colour::warn);
        else if (type == "src.rhythm" && asset->tempoBpm > 0.0)
        {
            g.setColour (colour::bone);
            g.drawText (juce::String (asset->tempoBpm, 1) + " BPM", inner, juce::Justification::centredRight);
        }
        return;
    }
    if (type == "proc.morph" || type == "proc.morph.global")
    {
        const bool voiceMorph = type == "proc.morph";
        const float w = inner.getWidth() / 14.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float a = juce::jlimit (0.0f, 1.0f, (tel (live, i) + 80.0f) / 80.0f);
            const float b = juce::jlimit (0.0f, 1.0f, (tel (live, 7 + i) + 80.0f) / 80.0f);
            g.setColour (colour::spore);
            g.fillRect (inner.getX() + w * static_cast<float> (2 * i), inner.getBottom() - inner.getHeight() * a, w - 1.0f, inner.getHeight() * a);
            g.setColour (colour::emberSoft);
            g.fillRect (inner.getX() + w * static_cast<float> (2 * i + 1), inner.getBottom() - inner.getHeight() * b, w - 1.0f, inner.getHeight() * b);
        }
        if (voiceMorph && tel (live, 14) < 0.5f)
            label ("PLAY TWO SOURCES", colour::boneFaint);
        return;
    }
    if (type == "spatial.satellite")
    {
        g.setColour (colour::sporeDim);
        g.drawHorizontalLine (static_cast<int> (inner.getCentreY()), inner.getX(), inner.getRight());
        g.setColour (colour::ember);
        const float x = inner.getCentreX() + inner.getWidth() * 0.5f * tel (live, 0);
        g.fillEllipse (x - 4.0f, inner.getCentreY() - 4.0f, 8.0f, 8.0f);
        return;
    }
    if (type == "osc.analog")
    {
        label (live != nullptr && tel (live, 0) > 0.0f ? juce::String (tel (live, 0), 1) + " Hz" : juce::String ("PLAY A NOTE"), colour::boneDim);
        return;
    }
    if (type == "util.voicemix")
    {
        label (juce::String (static_cast<int> (tel (live, 0))) + " VOICES", colour::bone);
        return;
    }
    if (type == "src.external")
    {
        label (s.externals().status (nodeId), colour::boneDim);
        return;
    }
    // default: a level meter (output peak) or a control value
    float level = live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
    if (type == "ctl.lfo" || type == "ctl.random" || type == "ctl.env" || type == "ctl.follower" || type == "proc.vca")
        level = std::abs (tel (live, 0));
    if (live != nullptr)
        const_cast<CompiledNode*> (live)->peak.store (level * 0.8f, std::memory_order_relaxed);   // decays between frames
    g.setColour (colour::sporeDim);
    g.fillRoundedRectangle (inner.withHeight (4.0f).withCentre (inner.getCentre()), 2.0f);
    g.setColour (level > 0.95f ? colour::error : colour::spore);
    g.fillRoundedRectangle (inner.withHeight (4.0f).withCentre (inner.getCentre()).withWidth (inner.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 2.0f);
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
    if (ledArea.contains (e.getPosition()) && ! group)
    {
        auto& s = canvas.session();
        s.perform ("bypass", [this] (Document& d) {
            edit::Result r;
            if (auto* n = d.findNode (nodeId))
            {
                n->enabled = ! n->enabled;
                r.ok = true;
                r.message = n->enabled ? "on" : "bypassed";
            }
            return r;
        });
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

void NodeView::mouseMove (const juce::MouseEvent& e)
{
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
        else
            setTooltip (group ? "Double-click to open the group" : "Drag to move; double-click for every control; right-click for more");
        repaint();
    }
}

void NodeView::mouseExit (const juce::MouseEvent&)
{
    hoverPort = -1;
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
    setTitle ("Patch canvas");
    zoom = static_cast<float> (model.document().view.zoom);
    pan = { static_cast<float> (model.document().view.panX), static_cast<float> (model.document().view.panY) };
    rebuild();
    startTimerHz (25);
}

Canvas::~Canvas() = default;

juce::Point<float> Canvas::toWorld (juce::Point<float> p) const noexcept { return p / zoom + pan; }
juce::Point<float> Canvas::toCanvas (juce::Point<float> w) const noexcept { return (w - pan) * zoom; }

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
        v->refreshControls();
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
        const auto topLeft = world - pan;
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
        // outside the group being edited: the rail at the canvas' edge
        out = { input ? static_cast<float> (getWidth()) - 6.0f : 6.0f, 60.0f + static_cast<float> (std::hash<std::string> {} (node) % 300) };
        return true;
    }
    auto* v = viewFor (owner);
    if (v == nullptr)
        return false;
    auto local = v->portPosition (node, port, input);
    if (! v->isGroup())
    {
        // a parameter target: the knob if it is on the card, else the header's right edge
        const auto* n = model.document().findNode (node);
        const auto* d = n != nullptr ? model.registry().find (n->type) : nullptr;
        if (input && d != nullptr && d->inputIndex (port) < 0 && d->paramIndex (port) >= 0)
        {
            local = { static_cast<float> (v->getWidth()) * 0.5f, 4.0f };
            for (auto* c : v->getChildren())
                if (auto* k = dynamic_cast<ParamKnob*> (c); k != nullptr && k->paramId() == port)
                    local = k->getBounds().toFloat().getCentre().withY (static_cast<float> (k->getY()));
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

void Canvas::paintCables (juce::Graphics& g)
{
    const auto& doc = model.document();
    for (const auto& c : doc.connections)
    {
        juce::Point<float> a, b;
        const bool va = ! visibleOwner (c.from).empty(), vb = ! visibleOwner (c.to).empty();
        if (! va && ! vb)
            continue;
        if (visibleOwner (c.from) == visibleOwner (c.to) && viewFor (visibleOwner (c.from)) != nullptr && viewFor (visibleOwner (c.from))->isGroup())
            continue;   // inside a collapsed group
        if (! endPoint (c.from, c.fromPort, false, a) || ! endPoint (c.to, c.toPort, true, b))
            continue;
        const auto* from = doc.findNode (c.from);
        const auto* fd = from != nullptr ? model.registry().find (from->type) : nullptr;
        PortKind kind = PortKind::audioMono;
        if (fd != nullptr && fd->outputIndex (c.fromPort) >= 0)
            kind = effectiveKind (fd->outputs[static_cast<std::size_t> (fd->outputIndex (c.fromPort))], from->domain);
        const bool hot = c.id == hoverCable || selected.count (c.from) > 0 || selected.count (c.to) > 0;
        auto path = cablePath (a, b);
        auto col = colour::port (kind).withAlpha (hot ? 0.95f : 0.45f);
        const float width = (kind == PortKind::audioStereo ? 2.4f : 1.6f) * (hot ? 1.4f : 1.0f);
        if (c.toParam)
        {
            juce::Path dashed;
            const float dash[] = { 5.0f, 4.0f };
            juce::PathStrokeType (width).createDashedStroke (dashed, path, dash, 2);
            g.setColour (col);
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (hot)
        {
            // where the signal is going: a moving dot (real only when the source is active)
            const auto* live = liveNode (model, c.from);
            const float level = live != nullptr ? live->peak.load (std::memory_order_relaxed) : 0.0f;
            if (level > 0.0005f || kind == PortKind::notes)
            {
                const float t = static_cast<float> ((frame % 50) / 50.0);
                const auto at = path.getPointAlongPath (path.getLength() * t);
                g.setColour (colour::ember);
                g.fillEllipse (at.x - 2.5f, at.y - 2.5f, 5.0f, 5.0f);
            }
        }
    }
}

void Canvas::paint (juce::Graphics& g)
{
    g.fillAll (colour::peat);
    // a quiet grid (every 32 world px, dots)
    const float step = 32.0f * zoom;
    if (step > 9.0f)
    {
        g.setColour (colour::line.withAlpha (0.45f));
        const float ox = std::fmod (-pan.x * zoom, step), oy = std::fmod (-pan.y * zoom, step);
        for (float x = ox; x < static_cast<float> (getWidth()); x += step)
            for (float y = oy; y < static_cast<float> (getHeight()); y += step)
                g.fillRect (x, y, 1.2f, 1.2f);
    }
    paintCables (g);
    if (drag == Drag::cable)
    {
        juce::Point<float> a;
        if (endPoint (cable.node, cable.port, ! cable.fromOutput, a))
        {
            const auto p = cable.fromOutput ? cablePath (a, dragNow) : cablePath (dragNow, a);
            g.setColour (colour::port (cable.kind));
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
    }
    if (drag == Drag::rubber)
    {
        const auto r = juce::Rectangle<float> (dragStart, dragNow);
        g.setColour (colour::ember.withAlpha (0.08f));
        g.fillRect (r);
        g.setColour (colour::ember.withAlpha (0.6f));
        g.drawRect (r, 1.0f);
    }
    // breadcrumb (inside a group)
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
            crumb << "  >  " << doc.findGroup (gid)->name;
        breadcrumbArea = juce::Rectangle<float> (12.0f, 12.0f, 24.0f + juce::GlyphArrangement::getStringWidth (text (12.0f, 600), crumb), 26.0f);
        g.setColour (colour::cardHigh);
        g.fillRoundedRectangle (breadcrumbArea, 4.0f);
        g.setColour (colour::ember);
        g.setFont (text (12.0f, 600));
        g.drawText ("<  " + crumb, breadcrumbArea.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    }
    if (doc.nodes.empty())
    {
        g.setColour (colour::boneDim);
        g.setFont (text (15.0f, 500));
        g.drawText ("Right-click (or Cmd/Ctrl + K) and type \"analog\" to start.", getLocalBounds(), juce::Justification::centred);
        g.setFont (text (12.5f));
        g.setColour (colour::boneFaint);
        g.drawText ("Drop a WAV, AIFF or FLAC here to play a recording.", getLocalBounds().withTrimmedTop (48), juce::Justification::centred);
    }
}

std::string Canvas::cableAt (juce::Point<float> p) const
{
    const auto& doc = model.document();
    std::string best;
    float bestDistance = 7.0f;
    for (const auto& c : doc.connections)
    {
        juce::Point<float> a, b;
        if (visibleOwner (c.from).empty() && visibleOwner (c.to).empty())
            continue;
        if (! endPoint (c.from, c.fromPort, false, a) || ! endPoint (c.to, c.toPort, true, b))
            continue;
        const auto path = cablePath (a, b);
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        const float d = nearest.getDistanceFrom (p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = c.id;
        }
    }
    return best;
}

void Canvas::timerCallback()
{
    ++frame;
    if ((frame % 3) == 0)
        for (auto& v : views)
            v->refreshControls();
    for (auto& v : views)
        v->repaint();
    if (! hoverCable.empty() || ! selected.empty())
        repaint();
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
        const auto r = juce::Rectangle<float> (at.x, at.y, static_cast<float> (v->getWidth()), static_cast<float> (v->getHeight()));
        box = first ? r : box.getUnion (r);
        first = false;
    }
    box = box.expanded (60.0f);
    // Readable first (75 .. 125 %): a patch wider than the window is centred and panned, not shrunk.
    zoom = juce::jlimit (0.75f, 1.25f, std::min (static_cast<float> (getWidth()) / box.getWidth(), static_cast<float> (getHeight()) / box.getHeight()));
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
                return isParam ? edit::modulate (d, model.registry(), from, fromPort, to, toPort, 0.5) : edit::connect (d, model.registry(), from, fromPort, to, toPort);
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
                    auto r = cable.fromOutput ? edit::connect (doc, model.registry(), cable.node, cable.port, v->id(), p.id)
                                              : edit::connect (doc, model.registry(), v->id(), p.id, cable.node, cable.port);
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
