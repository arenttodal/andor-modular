#pragma once

#include "Session.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace andor::modular::ui
{

/** A parameter of a module, as a knob: drag (fine with Shift), double-click to reset, type a
    value from the menu, map it to a macro or a host slot. Shows the live value (macros, scenes,
    modulation) as an ember ring. */
class ParamKnob final : public juce::Slider
{
public:
    ParamKnob (Session& s, std::string node, std::string param, bool showLabel = true);
    void refresh();                             ///< reads the document and the live value
    const std::string& nodeId() const noexcept { return node; }
    const std::string& paramId() const noexcept { return param; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    static constexpr int labelHeight = 14;

private:
    void valueChanged() override;
    void startedDragging() override;
    void showMenu();
    const ParamDesc* desc() const;
    Session& session;
    std::string node, param;
    bool label = true, updating = false;
};

/** A choice parameter as a compact menu. */
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

/** A toggle parameter as a small switch with an LED. */
class ParamToggle final : public juce::Button
{
public:
    ParamToggle (Session& s, std::string node, std::string param);
    void refresh();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    using juce::Button::clicked;
    void clicked() override;
    Session& session;
    std::string node, param;
};

/** Builds the right control for a parameter. */
std::unique_ptr<juce::Component> makeControl (Session& s, const std::string& node, const ParamDesc& p);
void refreshControl (juce::Component* c);

/** The parameter context menu (shared by knobs, choices and toggles). */
void showParamMenu (Session& s, const std::string& node, const std::string& param, juce::Component* target);

/** A short message at the bottom of the window, with UNDO when the action can be undone. */
class Toast final : public juce::Component, private juce::Timer
{
public:
    Toast();
    void show (const juce::String& text, bool canUndo, bool isError);
    std::function<void()> onUndo;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String message;
    bool error = false;
    juce::TextButton undo { "UNDO" };
    int ticks = 0;
};

/** A plain text button styled for the top bar. */
class BarButton final : public juce::TextButton
{
public:
    explicit BarButton (const juce::String& label, const juce::String& tip = {});
};

} // namespace andor::modular::ui
