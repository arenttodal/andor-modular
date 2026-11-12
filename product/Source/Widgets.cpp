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
        auto* w = new juce::AlertWindow (juce::String (pd->name), "Type a value (" + juce::String (pd->format (pd->min)) + " .. " + juce::String (pd->format (pd->max)) + ")",
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

    juce::int64 argb (juce::Colour c) { return static_cast<juce::int64> (c.getARGB()); }
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
ParamKnob::ParamKnob (Session& s, std::string n, std::string p, Style st) : session (s), node (std::move (n)), param (std::move (p)), style (st)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setRotaryParameters (LookAndFeel::rotaryStart, LookAndFeel::rotaryEnd, true);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRange (0.0, 1.0);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (180);
    setScrollWheelEnabled (true);
    setPopupDisplayEnabled (false, false, nullptr);
    getProperties().set ("mini", style == Style::tile);
    getProperties().set ("arc", argb (colour::accent));
    if (const auto* pd = desc())
    {
        getProperties().set ("bipolar", pd->min < 0.0 && pd->max > 0.0);
        setDoubleClickReturnValue (true, pd->normalise (pd->def));
        setTitle (juce::String (pd->name));
        setHelpText (juce::String (pd->name));
    }
    refresh();
}

const ParamDesc* ParamKnob::desc() const { return findParam (session, node, param); }

void ParamKnob::setInks (juce::Colour caption, juce::Colour captionHot, juce::Colour arc)
{
    captionInk = caption;
    captionHotInk = captionHot;
    getProperties().set ("arc", argb (arc));
    repaint();
}

juce::Rectangle<int> ParamKnob::knobArea() const
{
    auto r = getLocalBounds();
    if (style == Style::tile)
        return r.withTrimmedBottom (13).withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight() - 13), r.getHeight() - 13);
    if (style == Style::panel)
        return r.withTrimmedTop (20).withTrimmedBottom (22);
    return r.withTrimmedTop (16).withTrimmedBottom (17);
}

void ParamKnob::refresh()
{
    const auto* pd = desc();
    if (pd == nullptr)
        return;
    updating = true;
    setValue (pd->normalise (docValue (session, node, param)), juce::dontSendNotification);
    updating = false;
    // The live value (macros, scenes, host automation) and incoming modulation.
    bool moved = false;
    for (const auto& c : session.document().connections)
        moved = moved || (c.toParam && c.to == node && c.toPort == param);
    for (const auto& m : session.document().macros)
        for (const auto& mm : m.mappings)
            moved = moved || (mm.node == node && mm.param == param);
    float now = static_cast<float> (getValue());
    if (auto* g = session.graph())
        now = static_cast<float> (pd->normalise (g->effectiveParam (node, param)));
    const bool show = moved || std::abs (now - static_cast<float> (getValue())) > 0.002f;
    if (show != live || (show && std::abs (now - liveValue) > 0.001f))
    {
        live = show;
        liveValue = now;
        repaint();
    }
}

void ParamKnob::paint (juce::Graphics& g)
{
    const auto* pd = desc();
    const auto area = knobArea();
    const bool hot = isMouseOverOrDragging();
    const auto params = getRotaryParameters();
    getLookAndFeel().drawRotarySlider (g, area.getX(), area.getY(), area.getWidth(), area.getHeight(), static_cast<float> (valueToProportionOfLength (getValue())),
                                       params.startAngleRadians, params.endAngleRadians, *this);
    if (live)
    {
        // Where macros, scenes and modulation hold it now: a small bead riding the value ring.
        const float side = static_cast<float> (std::min (area.getWidth(), area.getHeight()));
        const float body = 0.5f * side / (style == Style::tile ? 1.2f : 1.42f);
        const float angle = params.startAngleRadians + (params.endAngleRadians - params.startAngleRadians) * juce::jlimit (0.0f, 1.0f, liveValue);
        const auto c = area.toFloat().getCentre().getPointOnCircumference (body * 1.13f, angle);
        g.setColour (captionHotInk.withAlpha (0.9f));
        g.fillEllipse (juce::Rectangle<float> (4.4f, 4.4f).withCentre (c));
        g.setColour (board::sage);
        g.fillEllipse (juce::Rectangle<float> (2.8f, 2.8f).withCentre (c));
    }
    if (pd == nullptr)
        return;
    const auto value = juce::String (pd->format (docValue (session, node, param)));
    if (style == Style::tile)
    {
        // Its caption; its value while it is touched.
        g.setColour (hot ? captionHotInk : captionInk);
        g.setFont (fonts::make (9.5f, Weight::medium, 0.05f));
        g.drawFittedText (hot ? value : juce::String (pd->name), getLocalBounds().removeFromBottom (13), juce::Justification::centred, 1, 0.75f);
    }
    else
    {
        const bool panel = style == Style::panel;
        g.setColour (captionInk);
        g.setFont (fonts::make (panel ? 11.5f : 10.5f, Weight::medium, 0.05f));
        g.drawFittedText (juce::String (pd->name), getLocalBounds().removeFromTop (panel ? 17 : 15), juce::Justification::centred, 1, 0.75f);
        g.setColour (hot ? captionHotInk : captionHotInk.withAlpha (0.82f));
        g.setFont (fonts::make (panel ? 13.0f : 12.0f, Weight::medium));
        g.drawFittedText (value, getLocalBounds().removeFromBottom (panel ? 19 : 16), juce::Justification::centred, 1, 0.8f);
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (colour::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
    }
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParamMenu (session, node, param, this);
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
    return juce::String (pd->name + ": " + pd->format (docValue (session, node, param)) + "  (" + pd->format (pd->min) + " .. " + pd->format (pd->max))
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

//==============================================================================
ParamChoice::ParamChoice (Session& s, std::string n, std::string p) : session (s), node (std::move (n)), param (std::move (p))
{
    if (const auto* pd = findParam (session, node, param))
    {
        for (std::size_t i = 0; i < pd->choices.size(); ++i)
            addItem (juce::String (pd->choices[i]), static_cast<int> (i) + 1);
        setTitle (juce::String (pd->name));
        setTooltip (juce::String (pd->name));
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
ParamMode::ParamMode (Session& s, std::string n, std::string p) : session (s), node (std::move (n)), param (std::move (p))
{
    if (const auto* pd = findParam (session, node, param))
    {
        setTitle (juce::String (pd->name));
        setTooltip (juce::String (pd->name) + ": click to choose");
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ParamMode::setInks (juce::Colour i, juce::Colour hot)
{
    ink = i;
    hotInk = hot;
    repaint();
}

namespace
{
    juce::String modeText (Session& s, const std::string& node, const std::string& param)
    {
        const auto* pd = findParam (s, node, param);
        if (pd == nullptr || pd->choices.empty())
            return {};
        const int index = juce::jlimit (0, static_cast<int> (pd->choices.size()) - 1, static_cast<int> (std::lround (docValue (s, node, param))));
        return juce::String (pd->choices[static_cast<std::size_t> (index)]).toUpperCase();
    }
}

int ParamMode::idealWidth() const
{
    const auto font = onDisplay ? fonts::make (11.5f, Weight::medium, 0.1f) : fonts::make (10.5f, Weight::medium, 0.07f);
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, modeText (session, node, param))) + 20;
}

void ParamMode::paint (juce::Graphics& g)
{
    const auto value = modeText (session, node, param);
    const bool hot = isMouseOver();
    const auto font = onDisplay ? fonts::make (11.5f, Weight::medium, 0.1f) : fonts::make (10.5f, Weight::medium, 0.07f);
    g.setFont (font);
    g.setColour (hot ? hotInk : ink);
    const auto r = getLocalBounds().toFloat();
    const float w = std::min (r.getWidth() - 14.0f, juce::GlyphArrangement::getStringWidth (font, value) + 2.0f);
    const float x = onDisplay ? r.getRight() - w - 14.0f : r.getX();
    g.drawFittedText (value, juce::Rectangle<float> (x, r.getY(), w, r.getHeight()).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    icon::draw (g, icon::Kind::down, juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x + w + 7.0f, r.getCentreY() + 0.5f }), hot ? hotInk : ink, 1.2f);
}

void ParamMode::mouseDown (const juce::MouseEvent&)
{
    const auto* pd = findParam (session, node, param);
    if (pd == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (pd->name));
    const int now = static_cast<int> (std::lround (docValue (session, node, param)));
    for (std::size_t i = 0; i < pd->choices.size(); ++i)
        menu.addItem (juce::String (pd->choices[i]), true, static_cast<int> (i) == now, [this, i] {
            session.beginGesture ("choose");
            session.setParam (node, param, static_cast<double> (i));
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

//==============================================================================
ParamToggle::ParamToggle (Session& s, std::string n, std::string p, bool onTile) : juce::Button ({}), session (s), node (std::move (n)), param (std::move (p)), tile (onTile)
{
    if (const auto* pd = findParam (session, node, param))
    {
        setButtonText (juce::String (pd->name));
        setTitle (juce::String (pd->name));
    }
    setClickingTogglesState (false);
    refresh();
}

void ParamToggle::setInks (juce::Colour i, juce::Colour lit)
{
    ink = i;
    litInk = lit;
    repaint();
}

void ParamToggle::refresh()
{
    const bool on = docValue (session, node, param) >= 0.5;
    if (on != getToggleState())
    {
        setToggleState (on, juce::dontSendNotification);
        if (tile)
            repaint();
        else
            skin::repaintKey (*this);
    }
}

void ParamToggle::clicked()
{
    session.beginGesture ("toggle");
    session.setParam (node, param, getToggleState() ? 0.0 : 1.0);
    refresh();
}

skin::KeyState ParamToggle::keyState() const
{
    if (getToggleState())
        return skin::KeyState::active;
    return isDown() ? skin::KeyState::pressed : (isOver() ? skin::KeyState::hover : skin::KeyState::normal);
}

void ParamToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    if (tile)
    {
        // On a device: a small light and its word (the rack's switch).
        const auto light = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 5.0f, r.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (light.expanded (1.2f));
        g.setColour (getToggleState() ? litInk : ink.withAlpha (0.35f));
        g.fillEllipse (light);
        g.setColour (getToggleState() || highlighted ? litInk : ink);
        g.setFont (fonts::make (10.5f, Weight::medium, 0.07f));
        g.drawFittedText (getButtonText().toUpperCase(), getLocalBounds().withTrimmedLeft (14), juce::Justification::centredLeft, 1, 0.75f);
        return;
    }
    if (! skin::drawnBySurface())
        draw::button (g, r.reduced (1.0f), 7.0f, getToggleState() || down, highlighted, colour::accentSoft);
    g.setColour (getToggleState() ? colour::text : colour::textSecondary.withAlpha (highlighted ? 1.0f : 0.9f));
    g.setFont (fonts::make (11.5f, Weight::medium, 0.05f));
    g.drawFittedText (getButtonText().toUpperCase(), getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.75f);
    if (hasKeyboardFocus (false))
    {
        g.setColour (colour::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (1.5f), 6.0f, 1.0f);
    }
}

//==============================================================================
void refreshControl (juce::Component* c)
{
    if (auto* k = dynamic_cast<ParamKnob*> (c))
        k->refresh();
    else if (auto* ch = dynamic_cast<ParamChoice*> (c))
        ch->refresh();
    else if (auto* m = dynamic_cast<ParamMode*> (c))
        m->refresh();
    else if (auto* t = dynamic_cast<ParamToggle*> (c))
        t->refresh();
}

//==============================================================================
KeyButton::KeyButton (const juce::String& label, skin::KeyRole r) : juce::Button (label), role (r)
{
    setButtonText (label);
    setWantsKeyboardFocus (true);
}

skin::KeyState KeyButton::keyState() const
{
    if (getToggleState())
        return skin::KeyState::active;
    return isDown() ? skin::KeyState::pressed : (isOver() ? skin::KeyState::hover : skin::KeyState::normal);
}

void KeyButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    if (! skin::drawnBySurface())
        draw::button (g, r.reduced (1.0f), skin::keyRadius (role), getToggleState() || down, highlighted, colour::accentSoft);
    const float alpha = isEnabled() ? 1.0f : 0.38f;
    const auto ink = (getToggleState() ? colour::text : colour::text.withAlpha (highlighted ? 0.95f : 0.82f)).withMultipliedAlpha (alpha);
    const auto label = getButtonText();
    const float h = fontHeight > 0.0f ? fontHeight : std::min (15.0f, r.getHeight() * 0.42f);
    const auto font = type::button (h);
    const float iconSize = std::min (r.getHeight() * 0.56f, 24.0f);
    const float textW = label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, label);
    const float gap = hasIcon && label.isNotEmpty() ? 4.0f : 0.0f;
    const float total = textW + (hasIcon ? iconSize : 0.0f) + gap;
    float x = r.getCentreX() - 0.5f * total;
    const float press = down ? 0.6f : 0.0f;
    if (hasIcon && ! iconTrailing)
    {
        icon::draw (g, glyph, juce::Rectangle<float> (x, r.getCentreY() - 0.5f * iconSize + press, iconSize, iconSize), ink, 1.5f);
        x += iconSize + gap;
    }
    if (label.isNotEmpty())
    {
        g.setColour (ink);
        g.setFont (font);
        g.drawText (label, juce::Rectangle<float> (x, r.getY() + press, textW + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        x += textW + gap;
    }
    if (hasIcon && iconTrailing)
        icon::draw (g, glyph, juce::Rectangle<float> (x, r.getCentreY() - 0.5f * iconSize + press, iconSize, iconSize), ink, 1.5f);
    if (hasKeyboardFocus (true) && ! isMouseOver())
    {
        g.setColour (colour::accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (2.0f), std::max (2.0f, skin::keyRadius (role) - 1.0f), 1.0f);
    }
}

//==============================================================================
InkButton::InkButton (const juce::String& label) : juce::Button (label)
{
    setButtonText (label);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int InkButton::idealWidth() const
{
    const float textW = getButtonText().isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (fonts::make (11.5f, Weight::medium, 0.09f), getButtonText());
    return juce::roundToInt (textW + (hasIcon ? 20.0f : 0.0f) + (hasIcon && textW > 0.0f ? 3.0f : 0.0f) + 14.0f);
}

void InkButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    auto c = getToggleState() ? colour::accent : (highlighted ? board::ink : ink);
    c = c.withMultipliedAlpha (isEnabled() ? (down ? 0.75f : 1.0f) : 0.32f);
    float x = r.getX() + 7.0f;
    if (hasIcon)
    {
        icon::draw (g, glyph, juce::Rectangle<float> (x, r.getCentreY() - 9.0f, 18.0f, 18.0f), c, 1.4f);
        x += 21.0f;
    }
    g.setColour (c);
    g.setFont (fonts::make (11.5f, Weight::medium, 0.09f));
    g.drawText (getButtonText(), juce::Rectangle<float> (x, r.getY(), r.getRight() - x, r.getHeight()), juce::Justification::centredLeft, false);
    if (hasKeyboardFocus (true) && ! isMouseOver())
    {
        g.setColour (colour::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (1.5f, 4.0f), 4.0f, 1.0f);
    }
}

//==============================================================================
StatusLine::StatusLine()
{
    addChildComponent (undo);
    undo.setInk (colour::accent.darker (0.15f));
    undo.onClick = [this] {
        if (onUndo)
            onUndo();
        ticks = 0;
        message.clear();
        undo.setVisible (false);
        repaint();
    };
    setInterceptsMouseClicks (false, true);
}

void StatusLine::show (const juce::String& text, bool canUndo, bool isError)
{
    message = text;
    error = isError;
    undo.setVisible (canUndo);
    ticks = isError ? 150 : 110;
    resized();
    repaint();
    startTimerHz (30);
}

void StatusLine::setIdleText (const juce::String& text)
{
    if (text != idle)
    {
        idle = text;
        if (message.isEmpty())
            repaint();
    }
}

void StatusLine::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool speaking = message.isNotEmpty();
    // The family's status line: a small light, then what the instrument has to say.
    const juce::Point<float> light (r.getX() + 6.0f, r.getCentreY());
    draw::led (g, light, 7.0f, speaking ? (error ? juce::Colour (0xffb5482f) : colour::accent) : colour::textMicro.interpolatedWith (colour::housingBottom, 0.4f), speaking ? 0.7f : 0.0f);
    const float fade = speaking && ticks < 15 ? static_cast<float> (ticks) / 15.0f : 1.0f;
    g.setFont (type::micro (14.0f));
    g.setColour ((speaking ? (error ? juce::Colour (0xff9c3a22) : colour::text) : colour::textSecondary).withMultipliedAlpha (fade));
    auto text = getLocalBounds().withTrimmedLeft (18);
    if (undo.isVisible())
        text.setRight (undo.getX() - 6);
    g.drawText (speaking ? message : idle, text, juce::Justification::centredLeft, true);
}

void StatusLine::resized()
{
    const float w = juce::GlyphArrangement::getStringWidth (type::micro (14.0f), message);
    undo.setBounds (std::min (getWidth() - 60, 18 + juce::roundToInt (w) + 10), 0, 60, getHeight());
}

void StatusLine::timerCallback()
{
    if (--ticks <= 0)
    {
        stopTimer();
        message.clear();
        undo.setVisible (false);
    }
    if (ticks < 15)
        repaint();
}

} // namespace andor::modular::ui
