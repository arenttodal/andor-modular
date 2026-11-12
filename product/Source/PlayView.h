#pragma once

#include "Frame.h"

namespace andor::modular::ui
{

/** One of the eight macros in the family's macro panel: its name, the rendered ivory knob with
    its identity on a thin ring, its lens (lit while it moves something). A host parameter
    (automation recorded); a menu for its mappings (range, curve, remove), name and colour. */
class MacroKnob final : public juce::Component
{
public:
    MacroKnob (ModularProcessor& p, int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    int index() const noexcept { return slot; }

private:
    void showMenu();
    ModularProcessor& processor;
    int slot = 0;
    bool used = false;
    juce::String name;
    juce::Colour identity;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** The scenes, in the macro panel's right bay: four keys (I..IV), the morph between them on
    the family's rail, STORE. */
class ScenePanel final : public juce::Component
{
public:
    explicit ScenePanel (ModularProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void sceneClicked (int index, bool store);
    ModularProcessor& processor;
    Session& session;
    std::array<std::unique_ptr<KeyButton>, sceneCount> scenes;
    KeyButton store { "STORE", skin::KeyRole::modifier };
    RailFader fader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool storeArmed = false;
};

/** EVOLVE: a popover over the instrument. Intensity, locks, seed; one transaction; A/B by
    undo / redo. */
class EvolvePanel final : public juce::Component
{
public:
    explicit EvolvePanel (Session& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    std::function<void()> onClose;
    static constexpr int margin = 26;   ///< room around the card for its shadow
    static constexpr int cardWidth = 404, cardHeight = 368;

private:
    void run();
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (margin); }
    Session& session;
    KeyButton subtle { "SUBTLE", skin::KeyRole::modifier }, moderate { "MODERATE", skin::KeyRole::modifier }, wild { "WILD", skin::KeyRole::modifier };
    KeyButton lockScale { "KEEP THE SCALE", skin::KeyRole::modifier }, lockRhythm { "KEEP THE RHYTHM", skin::KeyRole::modifier },
        lockRouting { "KEEP THE ROUTING", skin::KeyRole::modifier }, keepLevel { "KEEP THE LEVELS", skin::KeyRole::modifier };
    KeyButton dice { "NEW SEED", skin::KeyRole::generic }, go { "EVOLVE", skin::KeyRole::generic }, compare { "A / B", skin::KeyRole::generic },
        keep { "KEEP", skin::KeyRole::generic };
    InkButton close { juce::String() };
    std::uint64_t seed = 1;
    bool evolved = false, showingOriginal = false;
    juce::String summary;
};

/** PLAY: the instrument's face on the board. Its name and what it is made of, and a graphite
    display of what it is playing now. */
class PlayView final : public juce::Component, private juce::Timer
{
public:
    explicit PlayView (ModularProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void timerCallback() override;
    ModularProcessor& processor;
    Session& session;
    juce::Rectangle<float> well;
    std::array<float, 1024> scope {};
};

} // namespace andor::modular::ui
