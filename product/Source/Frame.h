#pragma once

#include "Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace andor::modular
{
class ModularProcessor;
}

/**
    The parts of the ANDOR frame that Modular fills in: the family's preset bar, its VOLUME
    fader, its keyboard and PITCH wheel, the card beside the keyboard. Each is the family's
    own part (the same rendered pieces, tokens and drawing recipe: InstrumentFrame.cpp,
    MainSections.cpp), bound to Modular's patch instead of OSP's parameters.
*/
namespace andor::modular::ui
{

/** The preset bar: previous | the patch's name (opens the patches menu) | next. One key. */
class PresetBar final : public juce::Component, public skin::KeyFace, public juce::SettableTooltipClient
{
public:
    PresetBar();
    void setPatchName (const juce::String& name, bool edited);
    std::function<void()> onMenu, onPrevious, onNext;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    skin::KeyRole keyRole() const override { return skin::KeyRole::preset; }
    skin::KeyState keyState() const override;

private:
    int zoneAt (juce::Point<float>) const;   ///< 0 previous, 1 name, 2 next
    juce::String name;
    bool edited = false, down = false;
    int hover = -1;
};

/** VOLUME: the family's thin fader (the rendered cap on its rail), the patch's master level. */
class VolumeFader final : public juce::Slider
{
public:
    explicit VolumeFader (Session& s);
    void refresh();
    void paint (juce::Graphics&) override;
    static constexpr int thumbRadius = 7;

private:
    void valueChanged() override;
    void startedDragging() override;
    Session& session;
    bool updating = false;
};

/** PITCH: the family's sprung wheel in its slot; it bends the patch's notes. */
class PitchWheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PitchWheel (std::function<void (float)> onMove);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> slot() const { return getLocalBounds().toFloat().withHeight (88.0f); }
    std::function<void (float)> moved;
    float value = 0.0f, dragStart = 0.0f;
};

namespace detail
{
    struct KeyboardState
    {
        juce::MidiKeyboardState keys;
    };
}

/** The 88 keys (the family's keys in their bed): they play the patch, and show what the host
    plays. Nothing here is locked or read by the audio thread (ModularProcessor::playFromInterface). */
class Keyboard final : private detail::KeyboardState, public juce::MidiKeyboardComponent
{
public:
    explicit Keyboard (ModularProcessor&);
    ~Keyboard() override;

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour line, juce::Colour text) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour fill) override;
    juce::String getWhiteNoteText (int note) override;
    /** Between the keys and the processor: what is played here goes to the patch, what the
        host plays is shown here. */
    struct Relay final : public juce::MidiKeyboardState::Listener, private juce::Timer
    {
        Relay (ModularProcessor&, juce::MidiKeyboardState&);
        ~Relay() override;
        void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
        void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
        void timerCallback() override;
        ModularProcessor& processor;
        juce::MidiKeyboardState& keys;
        std::array<bool, 128> shown {};
        bool mirroring = false;
    };
    Relay relay;
};

/** The card beside the keyboard (the arpeggiator's place and key): how the patch's voices
    play. Its light is lit while a voice sounds. */
class VoicesCard final : public juce::Component, public skin::KeyFace, public juce::SettableTooltipClient
{
public:
    VoicesCard();
    void setText (const juce::String& mode, const juce::String& detail);
    void setSounding (bool sounding);
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::arp; }
    skin::KeyState keyState() const override;

private:
    juce::String mode { "POLY" }, detail { "8 VOICES" };
    bool sounding = false, down = false;
};

/** A linear value on the family's rail with its rendered cap (the scene morph). */
class RailFader final : public juce::Slider
{
public:
    RailFader();
    /** Marks under the rail (the four scenes). */
    void setStops (int count) { stops = count; }
    void paint (juce::Graphics&) override;

private:
    int stops = 0;
};

} // namespace andor::modular::ui
