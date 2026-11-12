#include "Frame.h"

#include "PluginProcessor.h"

namespace andor::modular::ui
{

//==============================================================================
PresetBar::PresetBar()
{
    setTitle ("Patch");
    setTooltip ("The patch: new, open, save, the factory patches and modules. The arrows step through the factory patches.");
    setWantsKeyboardFocus (true);
}

void PresetBar::setPatchName (const juce::String& n, bool isEdited)
{
    if (n != name || isEdited != edited)
    {
        name = n;
        edited = isEdited;
        repaint();
    }
}

int PresetBar::zoneAt (juce::Point<float> p) const
{
    if (p.x < 44.0f)
        return 0;
    return p.x > static_cast<float> (getWidth()) - 44.0f ? 2 : 1;
}

skin::KeyState PresetBar::keyState() const
{
    return down ? skin::KeyState::pressed : (hover >= 0 ? skin::KeyState::hover : skin::KeyState::normal);
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (! skin::drawnBySurface())
        draw::button (g, r.reduced (1.0f), skin::keyRadius (skin::KeyRole::preset), down, hover >= 0, colour::accentSoft);
    auto ink = [this] (int zone) { return colour::text.withAlpha (hover == zone ? 0.95f : 0.62f); };
    icon::draw (g, icon::Kind::previous, juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ 24.0f, r.getCentreY() }), ink (0), 1.6f);
    icon::draw (g, icon::Kind::next, juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ r.getRight() - 24.0f, r.getCentreY() }), ink (2), 1.6f);
    // The name, as the family writes a preset's; a quiet mark while it has unsaved changes.
    const auto font = type::preset (0.86f);
    g.setFont (font);
    g.setColour (colour::text.withAlpha (hover == 1 ? 1.0f : 0.92f));
    const auto area = r.reduced (48.0f, 0.0f).withTrimmedRight (22.0f);
    const auto shownName = name.isEmpty() ? juce::String ("Untitled") : name;
    const float w = std::min (area.getWidth(), juce::GlyphArrangement::getStringWidth (font, shownName) + 2.0f);
    const float x = r.getCentreX() - 0.5f * (w + 22.0f);
    g.drawFittedText (shownName, juce::Rectangle<float> (x, r.getY(), w, r.getHeight()).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    icon::draw (g, icon::Kind::down, juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x + w + 13.0f, r.getCentreY() + 1.0f }), ink (1), 1.5f);
    if (edited)
    {
        g.setColour (colour::accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x - 9.0f, r.getCentreY() }));
    }
}

void PresetBar::mouseDown (const juce::MouseEvent&)
{
    down = true;
    skin::repaintKey (*this);
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    skin::repaintKey (*this);
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    const int zone = zoneAt (e.position);
    if (auto& action = zone == 0 ? onPrevious : (zone == 2 ? onNext : onMenu))
        action();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    if (const int zone = zoneAt (e.position); zone != hover)
    {
        hover = zone;
        skin::repaintKey (*this);
    }
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    skin::repaintKey (*this);
}

//==============================================================================
VolumeFader::VolumeFader (Session& s) : session (s)
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    getProperties().set ("volume", true);   // (the family's thumb radius: the look-and-feel's getSliderThumbRadius)
    setRange (-48.0, 6.0, 0.1);
    setSkewFactorFromMidPoint (-10.0);
    setDoubleClickReturnValue (true, 0.0);
    setTitle ("Volume");
    setTooltip ("The patch's level. Double-click for 0 dB.");
    refresh();
}

void VolumeFader::refresh()
{
    updating = true;
    setValue (session.document().settings.masterDb, juce::dontSendNotification);
    updating = false;
}

void VolumeFader::startedDragging() { session.beginGesture ("volume"); }

void VolumeFader::valueChanged()
{
    if (! updating)
        session.setMasterDb (getValue());
}

void VolumeFader::paint (juce::Graphics& g)
{
    // MainSections.cpp, VolumeSlider::paint: the same caption, rail, accent line and cap.
    const auto bounds = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging();
    const auto caption = bounds.withHeight (18.0f).reduced (static_cast<float> (thumbRadius) - 1.0f, 0.0f);
    g.setFont (fonts::make (12.0f, Weight::medium, 0.12f));
    g.setColour (colour::textSecondary.withAlpha (hot ? 1.0f : 0.9f));
    g.drawText ("VOLUME", caption, juce::Justification::centredLeft, false);
    g.setFont (fonts::make (12.5f, Weight::regular));
    g.setColour (colour::text.withAlpha (hot ? 0.9f : 0.72f));
    const double db = getValue();
    g.drawText ((db > 0.05 ? "+" : "") + juce::String (std::abs (db) < 0.05 ? 0.0 : db, 1) + " dB", caption, juce::Justification::centredRight, false);

    const float cy = bounds.getBottom() - 11.0f;
    const float left = static_cast<float> (thumbRadius), right = bounds.getWidth() - static_cast<float> (thumbRadius);
    const float x = static_cast<float> (getPositionOfValue (getValue()));
    if (auto* pieces = skin::active())
    {
        const juce::Rectangle<float> rail (left - 1.6f, cy - 1.6f, right - left + 3.2f, 3.2f);
        const auto cap = juce::Rectangle<float> (14.0f, 22.0f).withCentre ({ x, cy });
        const bool drawn = pieces->draw (g, "rail", rail);
        if (drawn)
        {
            g.setColour (colour::accent.withAlpha (hot ? 1.0f : 0.92f));
            g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.6f, std::max (0.0f, x - left), 1.2f), 0.6f);
        }
        if (drawn && pieces->draw (g, isMouseButtonDown() ? "fader-drag" : hot ? "fader-hover" : "fader-normal", cap))
            return;
    }
    g.setColour (juce::Colour (0xffbdb2a2));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.75f, right - left, 1.5f), 0.75f);
    g.setColour (colour::accent.withAlpha (0.8f));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.75f, std::max (0.0f, x - left), 1.5f), 0.75f);
    draw::raised (g, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, cy }), 6.0f, colour::knobCapTop, colour::knobCapBottom, 0.6f);
}

//==============================================================================
RailFader::RailFader()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    getProperties().set ("volume", true);
}

void RailFader::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging();
    const float cy = bounds.getCentreY() - (stops > 0 ? 3.0f : 0.0f);
    const float left = 7.0f, right = bounds.getWidth() - 7.0f;
    const float x = static_cast<float> (getPositionOfValue (getValue()));
    if (stops > 1)
    {
        g.setColour (colour::textMicro.withAlpha (0.75f));
        for (int i = 0; i < stops; ++i)
        {
            const float sx = left + (right - left) * static_cast<float> (i) / static_cast<float> (stops - 1);
            g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ sx, cy + 13.0f }));
        }
    }
    bool drawn = false;
    if (auto* pieces = skin::active())
    {
        drawn = pieces->draw (g, "rail", juce::Rectangle<float> (left - 1.6f, cy - 1.6f, right - left + 3.2f, 3.2f));
        if (drawn)
        {
            g.setColour (colour::accent.withAlpha (hot ? 1.0f : 0.92f));
            g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.6f, std::max (0.0f, x - left), 1.2f), 0.6f);
            drawn = pieces->draw (g, isMouseButtonDown() ? "fader-drag" : hot ? "fader-hover" : "fader-normal", juce::Rectangle<float> (14.0f, 22.0f).withCentre ({ x, cy }));
        }
    }
    if (! drawn)
    {
        g.setColour (juce::Colour (0xffbdb2a2));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.75f, right - left, 1.5f), 0.75f);
        draw::raised (g, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, cy }), 6.0f, colour::knobCapTop, colour::knobCapBottom, 0.6f);
    }
}

//==============================================================================
PitchWheel::PitchWheel (std::function<void (float)> onMove) : moved (std::move (onMove))
{
    setTitle ("Pitch");
    setTooltip ("Pitch bend: drag up or down; it springs back.");
}

void PitchWheel::mouseDown (const juce::MouseEvent&) { dragStart = value; }

void PitchWheel::mouseDrag (const juce::MouseEvent& e)
{
    value = std::clamp (dragStart - static_cast<float> (e.getDistanceFromDragStartY()) / std::max (1.0f, slot().getHeight() * 0.5f), -1.0f, 1.0f);
    if (moved)
        moved (value);
    repaint();
}

void PitchWheel::mouseUp (const juce::MouseEvent&)
{
    value = 0.0f;
    if (moved)
        moved (value);
    repaint();
}

void PitchWheel::paint (juce::Graphics& g)
{
    // MainSections.cpp, Wheel::paint (the sprung PITCH wheel): a cream bezel around a dark
    // slot; in it the wheel through a narrow window, an amber segment riding from the middle.
    const auto s = slot();
    draw::raised (g, s, 7.0f, colour::cardTop, colour::cardBottom, 0.7f);
    const auto slotArea = juce::Rectangle<float> (s.getX() + 4.0f, s.getY() + 6.0f, s.getWidth() - 8.0f, s.getHeight() - 10.0f);
    g.setColour (juce::Colour (0xff15110d));
    g.fillRoundedRectangle (slotArea, 4.5f);
    const auto body = slotArea.reduced (1.5f, 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff57504a), 0.0f, body.getY(), juce::Colour (0xff201a15), 0.0f, body.getBottom(), false));
    g.fillRoundedRectangle (body, 3.5f);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRect (juce::Rectangle<float> (body.getX(), body.getY() + 3.0f, 1.5f, body.getHeight() - 6.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, body.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, body.getY() + 7.0f, false));
    g.fillRect (body.withHeight (7.0f));

    const auto window = juce::Rectangle<float> (slotArea.getX() + 0.23f * slotArea.getWidth(), body.getY() + 2.0f, 0.46f * slotArea.getWidth(), body.getHeight() - 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (window.getX() - 1.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));
    g.fillRect (juce::Rectangle<float> (window.getRight() + 0.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));

    const float length = 0.68f * slotArea.getHeight();
    const float centre = slotArea.getCentreY() + 0.07f * slotArea.getHeight() - value * 0.3f * slotArea.getHeight();
    const auto lit = juce::Rectangle<float> (window.getX(), centre - 0.5f * length, window.getWidth(), length).getIntersection (window);
    if (lit.getHeight() > 1.0f)
    {
        // The live accent, lit from above (the family's amber gradient in the accent).
        const auto a = colour::accent;
        juce::ColourGradient light (a.brighter (0.75f), 0.0f, lit.getY(), a.darker (0.55f), 0.0f, lit.getBottom(), false);
        light.addColour (0.12, a.brighter (0.65f));
        light.addColour (0.3, a.brighter (0.25f));
        light.addColour (0.5, a);
        light.addColour (0.75, a.darker (0.3f));
        g.setGradientFill (light);
        g.fillRoundedRectangle (lit, 2.5f);
        juce::ColourGradient across (juce::Colours::black.withAlpha (0.3f), lit.getX(), 0.0f, juce::Colours::black.withAlpha (0.4f), lit.getRight(), 0.0f, false);
        across.addColour (0.2, juce::Colours::black.withAlpha (0.0f));
        across.addColour (0.7, juce::Colours::black.withAlpha (0.0f));
        g.setGradientFill (across);
        g.fillRoundedRectangle (lit, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (lit.withHeight (1.0f).reduced (1.5f, 0.0f));
    }
    g.setColour (colour::text.withAlpha (0.8f));
    g.setFont (fonts::make (12.5f, Weight::medium, 0.06f));
    g.drawText ("PITCH", juce::Rectangle<float> (-12.0f, 88.0f, static_cast<float> (getWidth()) + 24.0f, 18.0f), juce::Justification::centred, false);
}

//==============================================================================
Keyboard::Keyboard (ModularProcessor& p) : juce::MidiKeyboardComponent (detail::KeyboardState::keys, horizontalKeyboard), relay (p, detail::KeyboardState::keys)
{
    // InstrumentFrame.cpp, setUpKeyboard.
    setAvailableRange (21, 108);
    setOctaveForMiddleC (4);   // MIDI 60 = C4, as everywhere in the family
    setScrollButtonsVisible (false);
    setBlackNoteLengthProportion (0.62f);
    setColour (shadowColourId, juce::Colours::transparentBlack);
    setColour (keySeparatorLineColourId, palette::hairline);
    setColour (whiteNoteColourId, palette::ivory);
    setColour (blackNoteColourId, palette::ebony);
    setTitle ("Keyboard");
    setWantsKeyboardFocus (false);   // (typing belongs to the patch: Quick Add, shortcuts)
}

Keyboard::~Keyboard() = default;

Keyboard::Relay::Relay (ModularProcessor& p, juce::MidiKeyboardState& state) : processor (p), keys (state)
{
    keys.addListener (this);
    startTimerHz (30);
}

Keyboard::Relay::~Relay() { keys.removeListener (this); }

void Keyboard::Relay::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    if (mirroring)
        return;
    MidiMessage m;
    m.kind = MidiMessage::Kind::noteOn;
    m.note = static_cast<std::uint8_t> (note);
    m.value = velocity;
    processor.playFromInterface (m);
}

void Keyboard::Relay::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    if (mirroring)
        return;
    MidiMessage m;
    m.kind = MidiMessage::Kind::noteOff;
    m.note = static_cast<std::uint8_t> (note);
    processor.playFromInterface (m);
}

void Keyboard::Relay::timerCallback()
{
    // What the host plays, shown on the keys (channel 2 of this display's own state, so a
    // key held by the mouse is never released by it).
    mirroring = true;
    for (int n = 21; n <= 108; ++n)
    {
        const bool held = processor.hostHolds (n);
        if (held != shown[static_cast<std::size_t> (n)])
        {
            shown[static_cast<std::size_t> (n)] = held;
            if (held)
                keys.noteOn (2, n, 0.8f);
            else
                keys.noteOff (2, n, 0.0f);
        }
    }
    mirroring = false;
}

void Keyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour, juce::Colour)
{
    // InstrumentFrame.cpp, OspKeyboard::drawWhiteNote: warm white keys with a rounded front, a
    // hairline between them; a held key only warms.
    const auto key = area.withTrimmedRight (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (key.getX(), key.getY() - 4.0f, key.getWidth(), key.getHeight() + 4.0f, 3.5f, 3.5f, false, false, true, true);
    auto top = colour::whiteKeyTop, bottom = colour::whiteKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.12f);
        bottom = bottom.interpolatedWith (colour::accent, 0.16f);
    }
    else if (isOver)
        top = top.darker (0.02f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, key.getY(), bottom, 0.0f, key.getBottom(), false));
    g.fillPath (shape);
    g.setColour (colour::keyLine);
    g.fillRect (juce::Rectangle<float> (area.getRight() - 1.0f, area.getY(), 1.0f, area.getHeight()));
    const auto label = getWhiteNoteText (note);
    if (label.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (std::min (14.0f, area.getWidth() * 0.6f), Weight::regular, 0.04f));
        const auto line = area.withTrimmedBottom (7.0f).removeFromBottom (16.0f);
        g.drawText (label, line.withX (area.getX() + 0.2f * area.getWidth()).withWidth (std::max (area.getWidth(), 24.0f)), juce::Justification::centredLeft, false);
    }
}

void Keyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    // InstrumentFrame.cpp, OspKeyboard::drawBlackNote.
    const auto r = area.reduced (0.8f, 0.0f).withTrimmedTop (-4.0f);
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 2.5f, 2.5f, false, false, true, true);
    design::CachedShadow (juce::Colour (0x55000000), 3, { 1, 2 }).drawForPath (g, shape);
    auto top = colour::blackKeyTop, bottom = colour::blackKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.2f);
        bottom = bottom.interpolatedWith (colour::accent, 0.14f);
    }
    else if (isOver)
        top = top.brighter (0.15f);
    g.setGradientFill (juce::ColourGradient (bottom, r.getX(), 0.0f, bottom, r.getRight(), 0.0f, false));
    g.fillPath (shape);
    juce::ColourGradient body (top.withAlpha (0.0f), r.getX(), 0.0f, top.withAlpha (0.0f), r.getRight(), 0.0f, false);
    body.addColour (0.45, top);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r.reduced (1.5f, 0.0f).withTrimmedBottom (0.12f * r.getHeight()), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRoundedRectangle (r.withTop (r.getBottom() - 0.12f * r.getHeight()).reduced (1.5f, 1.0f), 1.5f);
}

juce::String Keyboard::getWhiteNoteText (int note) { return note % 12 == 0 ? "C" + juce::String (note / 12 - 1) : juce::String(); }

//==============================================================================
VoicesCard::VoicesCard()
{
    setTitle ("Voices");
    setTooltip ("How the patch's voices play: poly (voices for each independent chain), mono or legato.");
}

void VoicesCard::setText (const juce::String& m, const juce::String& d)
{
    if (m != mode || d != detail)
    {
        mode = m;
        detail = d;
        repaint();
    }
}

void VoicesCard::setSounding (bool s)
{
    if (s != sounding)
    {
        sounding = s;
        repaint();
    }
}

skin::KeyState VoicesCard::keyState() const
{
    return down ? skin::KeyState::pressed : (isMouseOver() ? skin::KeyState::hover : skin::KeyState::normal);
}

void VoicesCard::mouseDown (const juce::MouseEvent&)
{
    down = true;
    skin::repaintKey (*this);
}

void VoicesCard::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    skin::repaintKey (*this);
    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

void VoicesCard::paint (juce::Graphics& g)
{
    // The arpeggiator's card: its light, its word, a chevron; under them what is chosen.
    const auto r = getLocalBounds().toFloat();
    if (! skin::drawnBySurface())
        draw::button (g, r.reduced (1.0f), skin::keyRadius (skin::KeyRole::arp), down, isMouseOver(), colour::accentSoft);
    const juce::Point<float> light (r.getX() + 16.0f, r.getY() + 22.0f);
    bool drawn = false;
    if (auto* pieces = skin::active())
        drawn = pieces->draw (g, sounding ? "led-arp-on" : "led-arp-off", juce::Rectangle<float> (11.0f, 11.0f).withCentre (light));
    if (! drawn)
        draw::led (g, light, 8.0f, sounding ? colour::arp.withMultipliedBrightness (1.15f) : colour::arp.interpolatedWith (colour::panelBottom, 0.6f), sounding ? 0.8f : 0.0f);
    g.setColour (colour::text.withAlpha (0.9f));
    g.setFont (fonts::make (14.5f, Weight::medium, 0.06f));
    g.drawText (mode, juce::Rectangle<float> (r.getX() + 28.0f, r.getY() + 12.0f, r.getWidth() - 50.0f, 20.0f), juce::Justification::centredLeft, false);
    icon::draw (g, icon::Kind::down, juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ r.getRight() - 16.0f, r.getY() + 22.0f }), colour::text.withAlpha (0.7f), 1.4f);
    g.setColour (colour::textSecondary);
    g.setFont (fonts::make (11.0f, Weight::medium, 0.08f));
    g.drawText (detail, juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 36.0f, r.getWidth() - 20.0f, 18.0f), juce::Justification::centredLeft, false);
}

} // namespace andor::modular::ui
