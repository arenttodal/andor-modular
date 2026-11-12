#include "ExternalHost.h"

namespace andor::modular
{

class ExternalHost::Hosted final : public ExternalInstrument
{
public:
    Hosted (std::string file, juce::String why) : path (std::move (file)), problem (std::move (why)) {}

    std::string name() const override { return instance != nullptr ? instance->getName().toStdString() : path; }
    bool isLoaded() const noexcept override { return instance != nullptr; }

    void prepare (double sampleRate, int maxBlock) override
    {
        if (instance == nullptr)
            return;
        // Prepared once per rate: a patch edit never re-prepares (and so never resets) the
        // instrument.
        if (juce::exactlyEqual (preparedRate, sampleRate) && preparedBlock >= maxBlock)
            return;
        instance->setRateAndBufferSizeDetails (sampleRate, std::max (maxBlock, 32));
        instance->prepareToPlay (sampleRate, std::max (maxBlock, 32));
        const int channels = std::max ({ 2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels() });
        buffer.setSize (channels, std::max (maxBlock, 32), false, true, false);
        midi.ensureSize (8192);
        preparedRate = sampleRate;
        preparedBlock = maxBlock;
    }

    void render (const NoteEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept override
    {
        if (instance == nullptr || numSamples > buffer.getNumSamples())
            return;
        midi.clear();
        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[i];
            const int ch = std::clamp<int> (e.channel, 1, 16);
            if (e.kind == NoteEvent::Kind::noteOn)
                midi.addEvent (juce::MidiMessage::noteOn (ch, e.note, std::clamp (e.velocity, 0.0f, 1.0f)), e.offset);
            else if (e.kind == NoteEvent::Kind::noteOff)
                midi.addEvent (juce::MidiMessage::noteOff (ch, e.note), e.offset);
            else
                midi.addEvent (juce::MidiMessage::allNotesOff (ch), e.offset);
        }
        // A view of the prepared buffer at this chunk's length (no allocation).
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
        view.clear();
        instance->processBlock (view, midi);
        std::copy (view.getReadPointer (0), view.getReadPointer (0) + numSamples, left);
        std::copy (view.getReadPointer (std::min (1, view.getNumChannels() - 1)), view.getReadPointer (std::min (1, view.getNumChannels() - 1)) + numSamples, right);
    }

    int latencySamples() const noexcept override { return instance != nullptr ? instance->getLatencySamples() : 0; }

    std::string path;
    juce::String problem;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double preparedRate = 0.0;
    int preparedBlock = 0;
};

ExternalHost::ExternalHost()
{
    manager.addDefaultFormats();
}

ExternalHost::~ExternalHost()
{
    windows.clear();
    instances.clear();
}

std::shared_ptr<ExternalInstrument> ExternalHost::instanceFor (const std::string& nodeId, const std::vector<std::string>& strings)
{
    const std::string path = strings.size() > 0 ? strings[0] : std::string {};
    const std::string state = strings.size() > 1 ? strings[1] : std::string {};
    const auto it = instances.find (nodeId);
    if (it != instances.end() && it->second->path == path)
    {
        it->second->prepare (rate, block);
        return it->second;
    }
    std::shared_ptr<Hosted> hosted;
    if (path.empty())
       #if JUCE_MAC
        hosted = std::make_shared<Hosted> (path, "choose an instrument plugin (VST3 or AU)");
       #else
        hosted = std::make_shared<Hosted> (path, "choose an instrument plugin (VST3)");
       #endif
    else
    {
        juce::String error = "not found";
        std::unique_ptr<juce::AudioPluginInstance> instance;
        for (auto* format : manager.getFormats())
        {
            if (! format->fileMightContainThisPluginType (juce::String (path)))
                continue;
            juce::OwnedArray<juce::PluginDescription> types;
            format->findAllTypesForFile (types, juce::String (path));
            for (auto* t : types)
                if (t->isInstrument || types.size() == 1)
                {
                    instance = manager.createPluginInstance (*t, rate, block, error);
                    if (instance != nullptr)
                        break;
                }
            if (instance != nullptr)
                break;
        }
        hosted = std::make_shared<Hosted> (path, instance != nullptr ? juce::String {} : "not installed or not loadable: " + error);
        if (instance != nullptr && ! state.empty())
        {
            juce::MemoryBlock block64;
            if (block64.fromBase64Encoding (state))
                instance->setStateInformation (block64.getData(), static_cast<int> (block64.getSize()));
        }
        hosted->instance = std::move (instance);
    }
    hosted->prepare (rate, block);
    instances[nodeId] = hosted;
    return hosted;
}

void ExternalHost::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    block = maxBlock;
    for (auto& [id, h] : instances)
        h->prepare (rate, block);
}

std::map<std::string, std::string> ExternalHost::states() const
{
    std::map<std::string, std::string> out;
    for (const auto& [id, h] : instances)
        if (h->instance != nullptr)
        {
            juce::MemoryBlock mb;
            h->instance->getStateInformation (mb);
            out[id] = mb.toBase64Encoding().toStdString();
        }
    return out;
}

bool ExternalHost::openEditor (const std::string& nodeId)
{
    const auto it = instances.find (nodeId);
    if (it == instances.end() || it->second->instance == nullptr || ! it->second->instance->hasEditor())
        return false;
    auto& slot = windows[nodeId];
    if (slot == nullptr)
    {
        slot = std::make_unique<juce::DocumentWindow> (it->second->instance->getName(), juce::Colours::black, juce::DocumentWindow::closeButton);
        slot->setUsingNativeTitleBar (true);
        slot->setContentNonOwned (it->second->instance->createEditorIfNeeded(), true);
    }
    slot->setVisible (true);
    slot->toFront (true);
    return true;
}

juce::String ExternalHost::status (const std::string& nodeId) const
{
    const auto it = instances.find (nodeId);
    if (it == instances.end())
        return "no instrument";
    return it->second->instance != nullptr ? "HOSTING " + it->second->instance->getName() : it->second->problem;
}

void ExternalHost::retain (const std::vector<std::string>& nodeIds)
{
    for (auto it = instances.begin(); it != instances.end();)
    {
        if (std::find (nodeIds.begin(), nodeIds.end(), it->first) == nodeIds.end())
        {
            windows.erase (it->first);
            it = instances.erase (it);
        }
        else
            ++it;
    }
}

} // namespace andor::modular
