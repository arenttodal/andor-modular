#include "Widgets.h"

namespace andor::modular::ui
{

namespace
{
    const ParamDesc* findParam (Session& s, const std::string& node, const std::string& param)
    {
        const auto* n = s.document().findNode (node);
        const auto* d = n != nullptr ? s.registry().find (n->type) : nullptr;
        const int p = d != nullptr ? d->paramIndex (param) : -1;
        return p >= 0 ? &d->params[static_cast<std::size_t> (p)] : nullptr;
    }

    double docValue (Session& s, const std::string& node, const std::string& param)
    {
        return s.document().paramValue (s.registry(), node, param);
    }

    void typeValue (Session& s, const std::string& node, const std::string& param, juce::Component* target)
    {
        const auto* pd = findParam (s, node, param);
        if (pd == nullptr)
            return;
        auto* w = new juce::AlertWindow (pd->name, "Type a value (" + juce::String (pd->format (pd->min)) + " .. " + juce::String (pd->format (pd->max)) + ")",
                                         juce::MessageBoxIconType::NoIcon, target);
        w->addTextEditor ("value", juce::String (docValue (s, node, param), 4));
        w->addButton ("SET", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&s, node, param, w] (int result) {
                                if (result == 1)
                                {
                                    s.beginGesture ("type a value");
                                    s.setParam (node, param, w->getTextEditorContents ("value").getDoubleValue());
                                }
                            }),
                            true);
    }
}

void showParamMenu (Session& s, const std::string& node, const std::string& param, juce::Component* target)
{
    const auto* pd = findParam (s, node, param);
    if (pd == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (pd->name) + "  " + juce::String (pd->format (docValue (s, node, param))));
    menu.addItem ("Reset to " + juce::String (pd->format (pd->def)), [&s, node, param, def = pd->def] {
        s.beginGesture ("reset");
        s.setParam (node, param, def);
    });
    menu.addItem ("Type a value...", [&s, node, param, target] { typeValue (s, node, param, target); });

    juce::PopupMenu macros;
    for (int m = 0; m < maxMacros; ++m)
    {
        const auto& macro = s.document().macros[static_cast<std::size_t> (m)];
        bool mapped = false;
        for (const auto& mm : macro.mappings)
            mapped = mapped || (mm.node == node && mm.param == param);
        const juce::String label = juce::String (m + 1) + "  " + (macro.name.empty() ? juce::String ("(unused)") : juce::String (macro.name));
        macros.addItem (label, ! mapped, mapped, [&s, node, param, m] {
            s.perform ("map to macro " + juce::String (m + 1), [&] (Document& d) {
                edit::Result r;
                r.ok = edit::mapMacro (d, s.registry(), m, node, param, 0.0, 1.0);
                r.message = r.ok ? "macro " + std::to_string (m + 1) + " now moves " + param : "could not map";
                return r;
            });
        });
    }
    menu.addSubMenu ("Assign to macro", macros);

    juce::PopupMenu slots;
    for (int b = 0; b < hostBankSize; ++b)
    {
        const auto& hb = s.document().hostBindings[static_cast<std::size_t> (b)];
        const bool mine = hb.node == node && hb.param == param;
        juce::String label = "Assign " + juce::String (b + 1);
        if (! hb.node.empty() && ! mine)
            label << "  (" << hb.param << ")";
        slots.addItem (label, true, mine, [&s, node, param, b] {
            s.perform ("bind host slot", [&] (Document& d) {
                edit::Result r;
                for (auto& other : d.hostBindings)
                    if (other.node == node && other.param == param)
                        other = {};
                d.hostBindings[static_cast<std::size_t> (b)] = { node, param };
                r.ok = true;
                r.message = "host parameter Assign " + std::to_string (b + 1) + " moves " + param;
                return r;
            });
        });
    }
    menu.addSubMenu ("Automate from the host", slots);

    if (pd->modulatable)
    {
        juce::PopupMenu sources;
        for (const auto& n : s.document().nodes)
        {
            const auto* d = s.registry().find (n.type);
            if (d == nullptr || n.id == node)
                continue;
            for (const auto& o : d->outputs)
                if (isControlLike (o.kind))
                    sources.addItem (juce::String (s.document().displayName (s.registry(), n)) + "  " + juce::String (o.name), [&s, node, param, from = n.id, port = o.id] {
                        s.perform ("modulate", [&] (Document& doc) { return edit::modulate (doc, s.registry(), from, port, node, param, 0.3); });
                    });
        }
        if (sources.getNumItems() > 0)
            menu.addSubMenu ("Modulate from", sources);
        for (const auto& c : s.document().connections)
            if (c.toParam && c.to == node && c.toPort == param)
            {
                const auto* from = s.document().findNode (c.from);
                menu.addItem ("Remove modulation from " + juce::String (from != nullptr ? s.document().displayName (s.registry(), *from) : c.from),
                              [&s, id = c.id] {
                                  s.perform ("remove modulation", [&] (Document& d) {
                                      d.disconnect (id);
                                      edit::Result r;
                                      r.ok = true;
                                      r.message = "modulation removed";
                                      return r;
                                  });
                              });
            }
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

//==============================================================================
ParamKnob::ParamKnob (Session& s, std::string n, std::string p, bool showLabel) : session (s), node (std::move (n)), param (std::move (p)), label (showLabel)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRange (0.0, 1.0);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (180);
    setScrollWheelEnabled (true);
    setPopupDisplayEnabled (false, false, nullptr);
    if (const auto* pd = desc())
    {
        getProperties().set ("bipolar", pd->min < 0.0 && pd->max > 0.0);
        setDoubleClickReturnValue (true, pd->normalise (pd->def));
        setTitle (pd->name);
        setHelpText (pd->name);
    }
    refresh();
}

const ParamDesc* ParamKnob::desc() const { return findParam (session, node, param); }

void ParamKnob::refresh()
{
    const auto* pd = desc();
    if (pd == nullptr)
        return;
    updating = true;
    setValue (pd->normalise (docValue (session, node, param)), juce::dontSendNotification);
    updating = false;
    // The live value (macros, scenes, host automation) and incoming modulation.
    bool modulated = false;
    for (const auto& c : session.document().connections)
        modulated = modulated || (c.toParam && c.to == node && c.toPort == param);
    bool macro = false;
    for (const auto& m : session.document().macros)
        for (const auto& mm : m.mappings)
            macro = macro || (mm.node == node && mm.param == param);
    double live = getValue();
    if (auto* g = session.graph())
        live = pd->normalise (g->effectiveParam (node, param));
    const bool show = modulated || macro || std::abs (live - getValue()) > 0.002;
    getProperties().set ("modulated", show);
    getProperties().set ("modValue", live);
    repaint();
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    if (label)
    {
        const auto* pd = desc();
        auto text = r.removeFromBottom (labelHeight);
        g.setColour (isMouseOverOrDragging() ? colour::bone : colour::boneDim);
        g.setFont (ui::text (10.0f, 500));
        const auto value = pd != nullptr && isMouseOverOrDragging () ? juce::String (pd->format (docValue (session, node, param)))
                                                                    : (pd != nullptr ? juce::String (pd->name) : juce::String {});
        g.drawFittedText (value, text, juce::Justification::centred, 1, 0.8f);
    }
    const float v = static_cast<float> (valueToProportionOfLength (getValue()));
    const float m = static_cast<float> (static_cast<double> (getProperties().getWithDefault ("modValue", getValue())));
    drawKnob (g, r.toFloat().reduced (2.0f), v, getProperties().getWithDefault ("bipolar", false), m, getProperties().getWithDefault ("modulated", false),
              isMouseOverOrDragging(), colour::spore);
    if (hasKeyboardFocus (false))
    {
        g.setColour (colour::ember);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    Slider::mouseDown (e);
}

void ParamKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    session.beginGesture ("reset");
    Slider::mouseDoubleClick (e);
}

juce::String ParamKnob::getTooltip()
{
    const auto* pd = desc();
    if (pd == nullptr)
        return {};
    return pd->name + ": " + pd->format (docValue (session, node, param)) + "  (" + pd->format (pd->min) + " .. " + pd->format (pd->max)
           + ")\nDrag; Shift for fine; double-click resets; right-click for macros, automation and modulation.";
}

void ParamKnob::startedDragging()
{
    if (const auto* pd = desc())
        session.beginGesture ("change " + juce::String (pd->name));
}

void ParamKnob::valueChanged()
{
    if (updating)
        return;
    if (const auto* pd = desc())
        session.setParam (node, param, pd->denormalise (getValue()));
}

void ParamKnob::showMenu() { showParamMenu (session, node, param, this); }

//==============================================================================
ParamChoice::ParamChoice (Session& s, std::string n, std::string p) : session (s), node (std::move (n)), param (std::move (p))
{
    if (const auto* pd = findParam (session, node, param))
    {
        for (std::size_t i = 0; i < pd->choices.size(); ++i)
            addItem (pd->choices[i], static_cast<int> (i) + 1);
        setTitle (pd->name);
        setTooltip (pd->name);
    }
    onChange = [this] {
        if (updating)
            return;
        session.beginGesture ("choose");
        session.setParam (node, param, getSelectedId() - 1);
    };
    refresh();
}

void ParamChoice::refresh()
{
    updating = true;
    setSelectedId (static_cast<int> (std::lround (docValue (session, node, param))) + 1, juce::dontSendNotification);
    updating = false;
}

//==============================================================================
ParamToggle::ParamToggle (Session& s, std::string n, std::string p) : juce::Button ({}), session (s), node (std::move (n)), param (std::move (p))
{
    if (const auto* pd = findParam (session, node, param))
    {
        setButtonText (pd->name);
        setTitle (pd->name);
    }
    setClickingTogglesState (false);
    refresh();
}

void ParamToggle::refresh()
{
    setToggleState (docValue (session, node, param) >= 0.5, juce::dontSendNotification);
    repaint();
}

void ParamToggle::clicked()
{
    session.beginGesture ("toggle");
    session.setParam (node, param, getToggleState() ? 0.0 : 1.0);
    refresh();
}

void ParamToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (highlighted ? colour::raised.brighter (0.1f) : colour::raised);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (hasKeyboardFocus (false) ? colour::ember : colour::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto led = r.removeFromLeft (16.0f).withSizeKeepingCentre (6.0f, 6.0f);
    g.setColour (getToggleState() ? colour::ember : colour::sporeDim);
    g.fillEllipse (led);
    g.setColour (colour::bone);
    g.setFont (ui::text (10.0f, 500));
    g.drawFittedText (getButtonText(), r.toNearestInt().withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
std::unique_ptr<juce::Component> makeControl (Session& s, const std::string& node, const ParamDesc& p)
{
    if (p.curve == ParamCurve::choice)
        return std::make_unique<ParamChoice> (s, node, p.id);
    if (p.curve == ParamCurve::toggle)
        return std::make_unique<ParamToggle> (s, node, p.id);
    return std::make_unique<ParamKnob> (s, node, p.id);
}

void refreshControl (juce::Component* c)
{
    if (auto* k = dynamic_cast<ParamKnob*> (c))
        k->refresh();
    else if (auto* ch = dynamic_cast<ParamChoice*> (c))
        ch->refresh();
    else if (auto* t = dynamic_cast<ParamToggle*> (c))
        t->refresh();
}

//==============================================================================
Toast::Toast()
{
    addChildComponent (undo);
    undo.onClick = [this] {
        if (onUndo)
            onUndo();
        setVisible (false);
    };
    setInterceptsMouseClicks (false, true);
}

void Toast::show (const juce::String& text, bool canUndo, bool isError)
{
    message = text;
    error = isError;
    undo.setVisible (canUndo);
    ticks = isError ? 150 : 110;
    setVisible (true);
    setAlpha (1.0f);
    resized();
    repaint();
    startTimerHz (30);
}

void Toast::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colour::cardHigh.withAlpha (0.97f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (error ? colour::error : colour::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (error ? colour::error : colour::ember);
    g.fillEllipse (r.getX() + 12.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (colour::bone);
    g.setFont (text (13.0f));
    g.drawFittedText (message, getLocalBounds().withTrimmedLeft (28).withTrimmedRight (undo.isVisible() ? 84 : 12), juce::Justification::centredLeft, 2);
}

void Toast::resized() { undo.setBounds (getLocalBounds().removeFromRight (76).reduced (8, 7)); }

void Toast::timerCallback()
{
    if (--ticks <= 0)
    {
        stopTimer();
        setVisible (false);
    }
    else if (ticks < 15)
        setAlpha (static_cast<float> (ticks) / 15.0f);
}

BarButton::BarButton (const juce::String& text, const juce::String& tip) : juce::TextButton (text)
{
    setTooltip (tip);
    setWantsKeyboardFocus (true);
}

} // namespace andor::modular::ui
