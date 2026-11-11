#pragma once

#include "graph/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
    ANDOR Modular's visual tokens (docs/UI_COMPONENTS.md). The ANDOR language on a dark
    canvas: Peat ground, Bone type, Spore for the quiet working colour, and ANDOR's ember only
    where something is live or chosen. 8 px spacing grid; Inter for the interface, Outfit for
    the identity (ANDOR's own typefaces, SIL OFL).
*/
namespace andor::modular::ui
{

namespace colour
{
    const juce::Colour peat { 0xff100d0c };       ///< the ground
    const juce::Colour peatDeep { 0xff0b0908 };   ///< wells, the canvas' far grid
    const juce::Colour card { 0xff1b1716 };       ///< module cards
    const juce::Colour cardHigh { 0xff25201e };   ///< card headers, panels
    const juce::Colour raised { 0xff2f2926 };     ///< controls, buttons
    const juce::Colour line { 0xff3b3430 };       ///< hairlines
    const juce::Colour bone { 0xffe8e3da };       ///< primary text
    const juce::Colour boneDim { 0xffa39c92 };    ///< secondary text
    const juce::Colour boneFaint { 0xff6e675f };  ///< tertiary text, disabled
    const juce::Colour spore { 0xffa8ae9c };      ///< values, cables, arcs
    const juce::Colour sporeDim { 0xff5f6457 };
    const juce::Colour ember { 0xffe8692a };      ///< live, selected, modulation
    const juce::Colour emberSoft { 0xffc98b5a };
    const juce::Colour warn { 0xffd9a441 };
    const juce::Colour error { 0xffd2553f };
    /** Port kinds: a muted colour AND a shape each (never colour alone). */
    juce::Colour port (PortKind k) noexcept;
    /** Macro identities (8), from the same muted family. */
    juce::Colour macro (int index) noexcept;
}

namespace space
{
    constexpr int grid = 8;
    constexpr int topBar = 56;
    constexpr int detailWidth = 336;
    constexpr int nodeWidth = 184;
    constexpr int headerHeight = 28;
    constexpr int portSpacing = 20;
    constexpr int corner = 6;
}

/** The typefaces (embedded). */
juce::Font text (float height, int weight = 400);     ///< Inter: 400 regular, 500 medium, 600 semibold
juce::Font identity (float height, bool bold = false); ///< Outfit

/** A port's glyph: circle (audio, ring for stereo), triangle (notes), diamond (control),
    square (gate / trigger / clock), diamond with a bar (pitch). */
void drawPort (juce::Graphics& g, juce::Point<float> centre, PortKind kind, bool connected, bool highlight, float size = 10.0f);

/** A rotary value: a dark disc, its range arc, the value arc, and an optional modulation ring. */
void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float value01, bool bipolar, float modulation01, bool hasModulation,
               bool hover, juce::Colour accent);

/** The ANDOR / MODULAR wordmark. */
void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Font getPopupMenuFont() override { return text (14.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return text (12.5f, 500); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return text (12.5f, 500); }
    juce::Font getLabelFont (juce::Label&) override { return text (13.0f); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max, juce::Slider::SliderStyle,
                           juce::Slider&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

} // namespace andor::modular::ui
