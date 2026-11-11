#include "Theme.h"

#include "ModularData.h"

namespace andor::modular::ui
{

juce::Colour colour::port (PortKind k) noexcept
{
    switch (k)
    {
        case PortKind::notes: return juce::Colour (0xffc98b5a);
        case PortKind::audioMono:
        case PortKind::audioStereo: return juce::Colour (0xffe8e3da);
        case PortKind::control: return juce::Colour (0xffa8ae9c);
        case PortKind::pitch: return juce::Colour (0xffb7a6c9);
        case PortKind::gate:
        case PortKind::trigger:
        case PortKind::clock: return juce::Colour (0xff90a6ba);
    }
    return bone;
}

juce::Colour colour::macro (int index) noexcept
{
    static const juce::uint32 c[8] { 0xffe8692a, 0xffd9a441, 0xffa8ae9c, 0xff8fb3a6, 0xff90a6ba, 0xffb7a6c9, 0xffc98b5a, 0xffd8cbb5 };
    return juce::Colour (c[static_cast<std::size_t> ((index % 8 + 8) % 8)]);
}

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, static_cast<std::size_t> (size));
    }
    struct Faces
    {
        juce::Typeface::Ptr regular = load (ModularData::InterRegular_ttf, ModularData::InterRegular_ttfSize);
        juce::Typeface::Ptr medium = load (ModularData::InterMedium_ttf, ModularData::InterMedium_ttfSize);
        juce::Typeface::Ptr semibold = load (ModularData::InterSemiBold_ttf, ModularData::InterSemiBold_ttfSize);
        juce::Typeface::Ptr light = load (ModularData::OutfitExtraLight_ttf, ModularData::OutfitExtraLight_ttfSize);
        juce::Typeface::Ptr bold = load (ModularData::OutfitExtraBold_ttf, ModularData::OutfitExtraBold_ttfSize);
    };
    const Faces& faces()
    {
        static const Faces f;
        return f;
    }
}

juce::Font text (float height, int weight)
{
    const auto& f = faces();
    auto face = weight >= 600 ? f.semibold : (weight >= 500 ? f.medium : f.regular);
    return juce::Font (juce::FontOptions (face).withHeight (height));
}

juce::Font identity (float height, bool bold)
{
    const auto& f = faces();
    return juce::Font (juce::FontOptions (bold ? f.bold : f.light).withHeight (height));
}

void drawPort (juce::Graphics& g, juce::Point<float> c, PortKind kind, bool connected, bool highlight, float size)
{
    const auto col = colour::port (kind).withAlpha (connected || highlight ? 1.0f : 0.7f);
    const float r = size * 0.5f;
    juce::Path p;
    switch (kind)
    {
        case PortKind::audioMono:
        case PortKind::audioStereo:
            p.addEllipse (c.x - r, c.y - r, size, size);
            break;
        case PortKind::notes:
            p.addTriangle (c.x - r, c.y - r, c.x - r, c.y + r, c.x + r, c.y);
            break;
        case PortKind::control:
        case PortKind::pitch:
            p.startNewSubPath (c.x, c.y - r);
            p.lineTo (c.x + r, c.y);
            p.lineTo (c.x, c.y + r);
            p.lineTo (c.x - r, c.y);
            p.closeSubPath();
            break;
        default:
            p.addRectangle (c.x - r * 0.85f, c.y - r * 0.85f, size * 0.85f, size * 0.85f);
            break;
    }
    if (highlight)
    {
        g.setColour (colour::ember.withAlpha (0.35f));
        g.fillEllipse (c.x - size, c.y - size, 2.0f * size, 2.0f * size);
    }
    g.setColour (colour::peat);
    g.fillPath (p);
    g.setColour (col);
    if (connected)
        g.fillPath (p, juce::AffineTransform::scale (0.55f, 0.55f, c.x, c.y));
    g.strokePath (p, juce::PathStrokeType (1.4f));
    if (kind == PortKind::audioStereo)
        g.drawEllipse (c.x - r - 2.5f, c.y - r - 2.5f, size + 5.0f, size + 5.0f, 0.9f);
    if (kind == PortKind::pitch)
        g.drawLine (c.x - r, c.y + r + 2.0f, c.x + r, c.y + r + 2.0f, 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float v, bool bipolar, float mod, bool hasMod, bool hover, juce::Colour accent)
{
    const float d = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float r = d * 0.5f - 2.0f;
    constexpr float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
    // body: a dark ceramic disc with a soft top light
    juce::ColourGradient body (colour::raised.brighter (0.15f), c.x, c.y - r, colour::card, c.x, c.y + r, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    g.setColour (colour::line);
    g.drawEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, 1.0f);
    // the track and the value
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (colour::line.brighter (0.1f));
    g.strokePath (track, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float at = start + (end - start) * juce::jlimit (0.0f, 1.0f, v);
    const float from = bipolar ? start + (end - start) * 0.5f : start;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, at), std::max (from, at), true);
    g.setColour (hover ? accent.brighter (0.2f) : accent);
    g.strokePath (arc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (hasMod)
    {
        // the live modulated value: a thin ember ring outside the track
        const float mAt = start + (end - start) * juce::jlimit (0.0f, 1.0f, mod);
        juce::Path m;
        m.addCentredArc (c.x, c.y, r + 3.5f, r + 3.5f, 0.0f, std::min (at, mAt), std::max (at, mAt), true);
        g.setColour (colour::ember);
        g.strokePath (m, juce::PathStrokeType (1.6f));
        g.fillEllipse (c.x + (r + 3.5f) * std::sin (mAt) - 1.6f, c.y - (r + 3.5f) * std::cos (mAt) - 1.6f, 3.2f, 3.2f);
    }
    // pointer
    const float pr = r * 0.62f;
    g.setColour (colour::bone);
    g.drawLine (c.x + 0.25f * pr * std::sin (at), c.y - 0.25f * pr * std::cos (at), c.x + pr * std::sin (at), c.y - pr * std::cos (at), 1.8f);
}

void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colour::bone);
    g.setFont (identity (area.getHeight() * 0.62f, true));
    const juce::String andor ("ANDOR");
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), andor);
    g.drawText (andor, area.withWidth (w + 4.0f), juce::Justification::centredLeft);
    g.setColour (colour::boneDim);
    g.setFont (identity (area.getHeight() * 0.62f, false));
    g.drawText ("/MODULAR", area.withTrimmedLeft (w + 6.0f), juce::Justification::centredLeft);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::peat);
    setColour (juce::PopupMenu::backgroundColourId, colour::cardHigh);
    setColour (juce::PopupMenu::textColourId, colour::bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::bone);
    setColour (juce::PopupMenu::headerTextColourId, colour::boneDim);
    setColour (juce::TextButton::buttonColourId, colour::raised);
    setColour (juce::TextButton::buttonOnColourId, colour::ember);
    setColour (juce::TextButton::textColourOffId, colour::bone);
    setColour (juce::TextButton::textColourOnId, colour::peat);
    setColour (juce::ComboBox::backgroundColourId, colour::raised);
    setColour (juce::ComboBox::textColourId, colour::bone);
    setColour (juce::ComboBox::outlineColourId, colour::line);
    setColour (juce::ComboBox::arrowColourId, colour::boneDim);
    setColour (juce::Label::textColourId, colour::bone);
    setColour (juce::TextEditor::backgroundColourId, colour::peatDeep);
    setColour (juce::TextEditor::textColourId, colour::bone);
    setColour (juce::TextEditor::highlightColourId, colour::ember.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, colour::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::ember);
    setColour (juce::CaretComponent::caretColourId, colour::ember);
    setColour (juce::ListBox::backgroundColourId, colour::cardHigh);
    setColour (juce::ScrollBar::thumbColourId, colour::line.brighter (0.2f));
    setColour (juce::Slider::textBoxTextColourId, colour::bone);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, colour::cardHigh);
    setColour (juce::TooltipWindow::textColourId, colour::bone);
    setColour (juce::ToggleButton::textColourId, colour::bone);
    setColour (juce::ToggleButton::tickColourId, colour::ember);
    setColour (juce::AlertWindow::backgroundColourId, colour::cardHigh);
    setColour (juce::AlertWindow::textColourId, colour::bone);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? colour::ember : (down ? colour::line : (highlighted ? colour::raised.brighter (0.12f) : colour::raised)));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? colour::ember.brighter (0.2f) : colour::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    if (b.hasKeyboardFocus (true))
    {
        g.setColour (colour::ember.withAlpha (0.8f));
        g.drawRoundedRectangle (r.expanded (1.5f), 5.0f, 1.2f);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
    g.setColour (colour::raised);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colour::ember : colour::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = r.getRight() - 12.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (colour::boneDim);
    g.fillPath (arrow);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const bool hasMod = s.getProperties().getWithDefault ("modulated", false);
    const float mod = static_cast<float> (static_cast<double> (s.getProperties().getWithDefault ("modValue", pos)));
    drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, bipolar, mod, hasMod, s.isMouseOverOrDragging(), colour::spore);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle style,
                                    juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = static_cast<float> (y) + static_cast<float> (h) * 0.5f;
        g.setColour (colour::line);
        g.fillRoundedRectangle (static_cast<float> (x), cy - 2.0f, static_cast<float> (w), 4.0f, 2.0f);
        g.setColour (colour::spore);
        g.fillRoundedRectangle (static_cast<float> (x), cy - 2.0f, pos - static_cast<float> (x), 4.0f, 2.0f);
        g.setColour (colour::bone);
        g.fillRoundedRectangle (pos - 5.0f, cy - 9.0f, 10.0f, 18.0f, 3.0f);
        g.setColour (colour::peat);
        g.drawLine (pos, cy - 5.0f, pos, cy + 5.0f, 1.2f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, slider);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (colour::cardHigh);
    g.setColour (colour::line);
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
{
    g.fillAll (colour::cardHigh);
    g.setColour (colour::line);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (colour::bone);
    g.setFont (text (12.5f));
    g.drawFittedText (tip, juce::Rectangle<int> (0, 0, width, height).reduced (8, 4), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const int w = std::min (340, 24 + static_cast<int> (juce::GlyphArrangement::getStringWidth (text (12.5f), tip)));
    const int lines = 1 + static_cast<int> (juce::GlyphArrangement::getStringWidth (text (12.5f), tip)) / 320;
    const int h = 10 + 17 * lines;
    return juce::Rectangle<int> (screenPos.x + 12, screenPos.y + 16, w, h).constrainedWithin (parentArea);
}

} // namespace andor::modular::ui
