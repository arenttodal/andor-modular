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

/** One sound source on the front panel: ANDOR Analog's source card. Its badge, M and S (mute
    and solo: Session::setMuted / toggleSolo, ramps at the source's own output), its name and
    switches; its graphite display (the wave it makes, the recording it plays, its
    grains) with its modes in the corner; its knobs under it. Every control is the module's
    own parameter (the same one as on its tile in PATCH). The card's ceramic face is drawn
    by the surface below it (PlayView::cardAreas), so its shadow falls on the housing. */
class SourceCard final : public juce::Component, private juce::Timer
{
public:
    SourceCard (Session& s, std::string nodeId, int ordinal, bool compact);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (const std::string& nodeId)> onEdit;

private:
    void timerCallback() override;
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>);
    Session& session;
    std::string node;
    ModuleLook look;
    bool compact = false, audible = true;
    KeyButton mute { "M", skin::KeyRole::modifier }, solo { "S", skin::KeyRole::modifier };
    KeyButton edit { juce::String(), skin::KeyRole::generic };
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    std::vector<std::unique_ptr<ParamMode>> modes;
    std::vector<std::unique_ptr<ParamToggle>> toggles;
    juce::Rectangle<int> well;
};

/** PLAY: the instrument's front panel, as immediate as ANDOR Analog's: one card for each
    sound source across the band (one to four side by side, five or six in two rows), over
    the macro panel. No headline, no prose: the patch's name is in the preset bar. */
class PlayView final : public juce::Component
{
public:
    explicit PlayView (ModularProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Values changed, or the patch did: the cards follow (rebuilt only when the sources or
        what they show changed). */
    void refresh();
    /** Where the cards' faces go, in this component's coordinates. */
    std::vector<juce::Rectangle<int>> cardAreas() const;
    std::function<void (const std::string& nodeId)> onEdit;
    std::function<void()> onAddSource;

private:
    std::string signature() const;
    void rebuild();
    Session& session;
    std::vector<std::unique_ptr<SourceCard>> cards;
    KeyButton addSource { "ADD A SOURCE", skin::KeyRole::generic };
    std::string built;
};

} // namespace andor::modular::ui
