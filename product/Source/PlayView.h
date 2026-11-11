#pragma once

#include "Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace andor::modular
{
class ModularProcessor;
}

namespace andor::modular::ui
{

/** One of the eight macros: a host parameter (automation recorded), its name and colour from
    the patch, and a menu for its mappings (range, curve, remove), name and colour. */
class MacroKnob final : public juce::Component
{
public:
    MacroKnob (ModularProcessor& p, int index, bool large);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    int index() const noexcept { return slot; }

private:
    void showMenu();
    ModularProcessor& processor;
    int slot = 0;
    bool big = true;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** EVOLVE: intensity, locks, seed; one transaction; A/B compare by undo/redo. */
class EvolvePanel final : public juce::Component
{
public:
    explicit EvolvePanel (Session& s);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void run();
    Session& session;
    juce::TextButton subtle { "SUBTLE" }, moderate { "MODERATE" }, wild { "WILD" };
    juce::ToggleButton lockScale { "Keep the scale" }, lockRhythm { "Keep the rhythm" }, lockRouting { "Keep the routing" }, keepLevel { "Keep the levels" };
    juce::TextButton dice { "NEW SEED" }, go { "EVOLVE" }, compare { "A / B" }, keep { "KEEP" };
    juce::Label seedLabel;
    std::uint64_t seed = 1;
    bool evolved = false, showingOriginal = false;
    juce::String summary;
};

class PlayView final : public juce::Component, private juce::Timer
{
public:
    explicit PlayView (ModularProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onPatch, onSave;

private:
    void timerCallback() override;
    void sceneClicked (int index, bool store);
    ModularProcessor& processor;
    Session& session;
    std::vector<std::unique_ptr<MacroKnob>> macros;
    std::array<juce::TextButton, sceneCount> scenes;
    juce::TextButton store { "STORE" };
    juce::Slider sceneFader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sceneAttachment;
    juce::TextButton evolve { "EVOLVE" }, save { "SAVE" }, patch { "PATCH" };
    juce::Rectangle<int> visual, header;
    std::array<float, 1024> scope {};
    bool storeArmed = false;
};

} // namespace andor::modular::ui
