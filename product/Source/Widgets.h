#pragma once

#include "Session.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace andor::modular::ui
{

/** A parameter of a module as the instrument's knob (design::draw::knob through the family's
    look-and-feel): drag (Shift fine), double-click resets, right-click for macros, automation
    and modulation. On a tile it is the "mini" knob with its caption under it (its value while
    touched); in the detail column it has its tick scale, its label above and its value below.
    A thin sage ring shows the live value when macros, scenes or modulation move it. */
class ParamKnob final : public juce::Slider
{
public:
    enum class Style { tile, detail };
    ParamKnob (Session& s, std::string node, std::string param, Style style);
    /** The inks of the face it sits on (a device's enamel, or ceramic). */
    void setInks (juce::Colour caption, juce::Colour captionHot, juce::Colour arc);
    void refresh();
    const std::string& nodeId() const noexcept { return node; }
    const std::string& paramId() const noexcept { return param; }
    juce::Rectangle<int> knobArea() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    void valueChanged() override;
    void startedDragging() override;
    const ParamDesc* desc() const;
    Session& session;
    std::string node, param;
    Style style;
    juce::Colour captionInk { colour::textSecondary }, captionHotInk { colour::text };
    bool updating = false, live = false;
    float liveValue = 0.0f;
};

/** A choice parameter as the mode key (the family's ModeSelector look). */
class ParamChoice final : public juce::ComboBox
{
public:
    ParamChoice (Session& s, std::string node, std::string param);
    void refresh();

private:
    Session& session;
    std::string node, param;
    bool updating = false;
};

/** A choice on a tile: its value as the tile's mode line with a small chevron; a click
    opens its choices. */
class ParamMode final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamMode (Session& s, std::string node, std::string param);
    void setInks (juce::Colour ink, juce::Colour hot);
    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    Session& session;
    std::string node, param;
    juce::Colour ink { colour::textSecondary }, hotInk { colour::text };
};

/** A toggle parameter: in the detail column a modifier key (on: the accent's soft face); on a
    tile a small light and its word. */
class ParamToggle final : public juce::Button, public skin::KeyFace
{
public:
    ParamToggle (Session& s, std::string node, std::string param, bool onTile);
    void setInks (juce::Colour ink, juce::Colour lit);
    void refresh();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    skin::KeyRole keyRole() const override { return skin::KeyRole::modifier; }
    skin::KeyState keyState() const override;
    bool keyOnSurface() const override { return ! tile; }

private:
    using juce::Button::clicked;
    void clicked() override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    Session& session;
    std::string node, param;
    bool tile = false;
    juce::Colour ink { colour::textSecondary }, litInk { colour::accent };
};

void refreshControl (juce::Component* c);

/** The parameter menu (reset, type a value, macros, host slots, modulation). */
void showParamMenu (Session& s, const std::string& node, const std::string& param, juce::Component* target);

//==============================================================================
/** A ceramic key (skin::KeyFace: its rendered face and shadow are drawn by the surface it
    sits on, skin::drawKeys; it draws only its ink): a word, an icon, or both. */
class KeyButton final : public juce::Button, public skin::KeyFace
{
public:
    KeyButton (const juce::String& label, skin::KeyRole role = skin::KeyRole::generic);
    void setIcon (icon::Kind k, bool trailing = false)
    {
        glyph = k;
        hasIcon = true;
        iconTrailing = trailing;
    }
    void setFontHeight (float h) { fontHeight = h; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    skin::KeyRole keyRole() const override { return role; }
    skin::KeyState keyState() const override;

private:
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole role;
    icon::Kind glyph = icon::Kind::plus;
    bool hasIcon = false, iconTrailing = false;
    float fontHeight = 0.0f;
};

/** Ink only (the board's header: + ADD MODULE, ARRANGE, undo...): no face, darker on hover. */
class InkButton final : public juce::Button
{
public:
    explicit InkButton (const juce::String& label);
    void setIcon (icon::Kind k)
    {
        glyph = k;
        hasIcon = true;
    }
    void setInk (juce::Colour c) { ink = c; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    int idealWidth() const;

private:
    icon::Kind glyph = icon::Kind::plus;
    bool hasIcon = false;
    juce::Colour ink { board::soft };
};

/** The instrument's status line: what the last action did (with its UNDO), then the patch's
    state again. */
class StatusLine final : public juce::Component, private juce::Timer
{
public:
    StatusLine();
    void show (const juce::String& text, bool canUndo, bool isError);
    void setIdleText (const juce::String& text);
    std::function<void()> onUndo;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String message, idle { "Ready" };
    bool error = false;
    InkButton undo { "UNDO" };
    int ticks = 0;
};

} // namespace andor::modular::ui
