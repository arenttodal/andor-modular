#pragma once

#include "Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace andor::modular
{

/**
    ANDOR Modular's AudioProcessor. Host-visible parameters are a FIXED bank (their ids never
    change when modules move): 8 macros, the scene position, and 32 assignable slots that the
    patch binds to any module parameter by stable node id (Document::hostBindings). Everything
    else is the patch's own state, saved whole.
*/
class ModularProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    ModularProcessor();
    ~ModularProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return MODULAR_PRODUCT_NAME; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Session& session() noexcept { return sessionModel; }
    Engine& engine() noexcept { return audioEngine; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }

    /** The macro / scene parameters as the editor moves them (host automation recorded). */
    void setMacro (int index, float value);
    void setScene (float position);
    float cpuLoad() const noexcept { return load.load (std::memory_order_relaxed); }

    static juce::String macroId (int i) { return "macro" + juce::String (i + 1); }
    static juce::String slotId (int i) { return "assign" + juce::String (i + 1).paddedLeft ('0', 2); }

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    Engine audioEngine;
    juce::AudioProcessorValueTreeState apvts;
    Session sessionModel;
    std::array<std::atomic<float>*, maxMacros> macroParams {};
    std::atomic<float>* sceneParam = nullptr;
    std::array<std::atomic<float>*, hostBankSize> slotParams {};
    std::array<float, hostBankSize> slotValues {};
    std::vector<MidiMessage> midi;
    std::atomic<float> load { 0.0f };
    double rate = 48000.0;
    int reportedLatency = -1;
};

} // namespace andor::modular
