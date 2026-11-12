#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Theme.h"

namespace andor::modular
{

juce::AudioProcessorValueTreeState::ParameterLayout ModularProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < maxMacros; ++i)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { macroId (i), 1 }, "Macro " + juce::String (i + 1),
                                                            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "scene", 1 }, "Scene", juce::NormalisableRange<float> (0.0f, 3.0f), 0.0f));
    for (int i = 0; i < hostBankSize; ++i)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { slotId (i), 1 }, "Assign " + juce::String (i + 1),
                                                            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    return l;
}

ModularProcessor::ModularProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", layout()),
      sessionModel (audioEngine)
{
    // ANDOR Modular's finish (walnut, aged copper, bone ceramic): once, before any window.
    ui::applyMaterials();
    for (int i = 0; i < maxMacros; ++i)
        macroParams[static_cast<std::size_t> (i)] = apvts.getRawParameterValue (macroId (i));
    sceneParam = apvts.getRawParameterValue ("scene");
    for (int i = 0; i < hostBankSize; ++i)
        slotParams[static_cast<std::size_t> (i)] = apvts.getRawParameterValue (slotId (i));
    // The patch's macro positions become the parameters' values (now, and at every load).
    sessionModel.onMacroValue = [this] (int i, float v) {
        if (auto* p = apvts.getParameter (macroId (i)))
            p->setValueNotifyingHost (v);
    };
    sessionModel.pushMacros();
    midi.resize (4096);
    startTimerHz (30);
}

ModularProcessor::~ModularProcessor()
{
    stopTimer();
}

bool ModularProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void ModularProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    sessionModel.prepare (sampleRate, std::max (samplesPerBlock, 32));
    setLatencySamples (audioEngine.latencySamples());
}

void ModularProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn)
{
    juce::ScopedNoDenormals noDenormals;
    const auto started = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    int count = 0;
    // The instrument's own keyboard and wheel first (they were played before this block).
    {
        const auto scope = interfaceFifo.read (interfaceFifo.getNumReady());
        auto take = [&] (int start, int size) {
            for (int i = 0; i < size && count < static_cast<int> (midi.size()); ++i)
            {
                midi[static_cast<std::size_t> (count)] = interfaceQueue[static_cast<std::size_t> (start + i)];
                midi[static_cast<std::size_t> (count++)].offset = 0;
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }
    for (const auto meta : midiIn)
    {
        if (count >= static_cast<int> (midi.size()))
            break;
        const auto m = meta.getMessage();
        MidiMessage e;
        e.offset = meta.samplePosition;
        e.channel = static_cast<std::uint8_t> (std::clamp (m.getChannel(), 1, 16));
        if (m.isNoteOn())
        {
            e.kind = MidiMessage::Kind::noteOn;
            e.note = static_cast<std::uint8_t> (m.getNoteNumber());
            e.value = m.getFloatVelocity();
            hostNotes[e.note & 127].store (1, std::memory_order_relaxed);
        }
        else if (m.isNoteOff())
        {
            e.kind = MidiMessage::Kind::noteOff;
            e.note = static_cast<std::uint8_t> (m.getNoteNumber());
            hostNotes[e.note & 127].store (0, std::memory_order_relaxed);
        }
        else if (m.isPitchWheel())
        {
            e.kind = MidiMessage::Kind::pitchBend;
            e.value = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
        }
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
        {
            e.kind = MidiMessage::Kind::sustain;
            e.value = m.isSustainPedalOn() ? 1.0f : 0.0f;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            e.kind = MidiMessage::Kind::allNotesOff;
            for (auto& held : hostNotes)
                held.store (0, std::memory_order_relaxed);
        }
        else if (m.isController())
        {
            e.kind = MidiMessage::Kind::controller;
            e.controller = m.getControllerNumber();
            e.value = static_cast<float> (m.getControllerValue()) / 127.0f;
        }
        else
            continue;
        midi[static_cast<std::size_t> (count++)] = e;
    }

    osp::HostTiming timing;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            timing.valid = pos->getBpm().hasValue();
            timing.playing = pos->getIsPlaying();
            timing.bpm = pos->getBpm().orFallback (120.0);
            timing.ppq = pos->getPpqPosition().orFallback (0.0);
            if (const auto sig = pos->getTimeSignature())
            {
                timing.numerator = sig->numerator;
                timing.denominator = sig->denominator;
            }
        }

    for (int i = 0; i < maxMacros; ++i)
        audioEngine.setMacro (i, macroParams[static_cast<std::size_t> (i)]->load (std::memory_order_relaxed));
    audioEngine.setScenePosition (sceneParam->load (std::memory_order_relaxed));
    for (int i = 0; i < hostBankSize; ++i)
        slotValues[static_cast<std::size_t> (i)] = slotParams[static_cast<std::size_t> (i)]->load (std::memory_order_relaxed);

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (right == nullptr)
        audioEngine.process (left, left, n, midi.data(), count, timing, slotValues.data());   // mono bus: one channel written twice
    else
        audioEngine.process (left, right, n, midi.data(), count, timing, slotValues.data());
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
    const float now = static_cast<float> (elapsed / (static_cast<double> (n) / rate));
    load.store (0.9f * load.load (std::memory_order_relaxed) + 0.1f * now, std::memory_order_relaxed);
}

void ModularProcessor::timerCallback()
{
    sessionModel.tick();
    const int latency = audioEngine.latencySamples();
    if (latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
    }
    // CPU admission: sustained overload trims the voice pool (oldest notes are stolen first).
    const float l = cpuLoad();
    audioEngine.setVoiceCap (l > 0.85f ? std::max (2, audioEngine.activeVoices() - 2) : 0);
}

void ModularProcessor::playFromInterface (const MidiMessage& m) noexcept
{
    const auto scope = interfaceFifo.write (1);
    if (scope.blockSize1 > 0)
        interfaceQueue[static_cast<std::size_t> (scope.startIndex1)] = m;
    else if (scope.blockSize2 > 0)
        interfaceQueue[static_cast<std::size_t> (scope.startIndex2)] = m;
}

void ModularProcessor::setMacro (int index, float value)
{
    if (auto* p = apvts.getParameter (macroId (index)))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
}

void ModularProcessor::setScene (float position)
{
    if (auto* p = apvts.getParameter ("scene"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 3.0f, position)));
}

void ModularProcessor::getStateInformation (juce::MemoryBlock& data)
{
    // The parameters are the macros' truth: the patch saved with them.
    for (int i = 0; i < maxMacros; ++i)
        audioEngine.setMacro (i, macroParams[static_cast<std::size_t> (i)]->load());
    audioEngine.setScenePosition (sceneParam->load());
    juce::ValueTree root ("ANDOR_MODULAR_STATE");
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
    root.setProperty ("session", juce::String (sessionModel.toState()), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, data);
}

void ModularProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("ANDOR_MODULAR_STATE"))
        return;
    const auto params = root.getChildWithName (apvts.state.getType());
    // The patch first (without letting its stored macro positions reach the parameters), then
    // the host's own parameter state, which is the macros' truth (automation, saved projects).
    auto follow = std::move (sessionModel.onMacroValue);
    sessionModel.onMacroValue = nullptr;
    sessionModel.fromState (root["session"].toString().toStdString());
    sessionModel.onMacroValue = std::move (follow);
    if (params.isValid())
        apvts.replaceState (params);
    for (int i = 0; i < maxMacros; ++i)
        audioEngine.setMacro (i, macroParams[static_cast<std::size_t> (i)]->load());
    audioEngine.setScenePosition (sceneParam->load());
}

juce::AudioProcessorEditor* ModularProcessor::createEditor() { return new Editor (*this); }

} // namespace andor::modular

#if JucePlugin_Build_Product
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new andor::modular::ModularProcessor();
}
#endif
