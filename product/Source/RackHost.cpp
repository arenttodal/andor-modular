#include "RackHost.h"

namespace andor::modular::ui
{

/** The rack's parameter state needs an AudioProcessor to live in. This one is never prepared,
    never played and never shown to a host: it only carries the parameters. */
struct RackHost::Carrier final : public juce::AudioProcessor
{
    const juce::String getName() const override { return "rack"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

RackHost::RackHost (Session& s, std::string nodeId, Layout layout, std::vector<RackParam> binding)
    : session (s), node (std::move (nodeId)), bound (std::move (binding)), carrier (std::make_unique<Carrier>())
{
    state = std::make_unique<juce::AudioProcessorValueTreeState> (*carrier, nullptr, "RACK", std::move (layout));
    for (std::size_t i = 0; i < bound.size(); ++i)
    {
        auto* p = state->getParameter (bound[i].rackId);
        jassert (p != nullptr);   // (a binding names an ID the rack's schema has)
        if (p == nullptr)
            continue;
        byRackId[bound[i].rackId] = i;
        state->addParameterListener (bound[i].rackId, this);
        p->addListener (this);
    }
    pull();
}

RackHost::~RackHost()
{
    for (const auto& [id, index] : byRackId)
    {
        state->removeParameterListener (id, this);
        if (auto* p = state->getParameter (id))
            p->removeListener (this);
    }
}

double RackHost::toRack (const RackParam& b, double v) const { return b.toRack ? b.toRack (v) : v * b.scale + b.offset; }
double RackHost::toNode (const RackParam& b, double v) const { return b.toNode ? b.toNode (v) : (v - b.offset) / (b.scale != 0.0 ? b.scale : 1.0); }

std::string RackHost::paramFor (const juce::String& rackId) const
{
    const auto it = byRackId.find (rackId);
    return it != byRackId.end() ? bound[it->second].param : std::string();
}

void RackHost::hold (const juce::String& rackId, float value)
{
    held[rackId] = value;
    if (auto* p = state->getParameter (rackId))
    {
        const juce::ScopedValueSetter<bool> guard (pulling, true);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

void RackHost::pull()
{
    const auto& doc = session.document();
    if (doc.findNode (node) == nullptr)
        return;
    const juce::ScopedValueSetter<bool> guard (pulling, true);
    for (const auto& [id, index] : byRackId)
    {
        const auto& b = bound[index];
        auto* p = state->getParameter (id);
        const float target = p->convertTo0to1 (static_cast<float> (toRack (b, doc.paramValue (session.registry(), node, b.param))));
        if (std::abs (p->getValue() - target) > 1.0e-6f)
            p->setValueNotifyingHost (target);
    }
}

void RackHost::touch()
{
    // One step of the patch's history for one action in the rack: a drag, a click on a switch,
    // or a preset (which writes its parameters one after another, at once).
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! gesture && now - lastTouchMs > 60.0)
        session.beginGesture ("rack");
    lastTouchMs = now;
}

void RackHost::parameterGestureChanged (int index, bool starting)
{
    juce::ignoreUnused (index);
    if (pulling)
        return;
    if (starting)
        touch();
    gesture = starting;
    lastTouchMs = juce::Time::getMillisecondCounterHiRes();
}

void RackHost::parameterChanged (const juce::String& id, float value)
{
    if (pulling)
        return;
    const auto it = byRackId.find (id);
    if (it == byRackId.end())
        return;
    const auto& b = bound[it->second];
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return;   // (the rack's state has no other writer: nothing here is automated)
    if (! gesture)
        touch();
    session.setParam (node, b.param, toNode (b, static_cast<double> (value)));
    lastTouchMs = juce::Time::getMillisecondCounterHiRes();
    // What the module took may not be what was asked (its own range): the rack shows the module.
    const double taken = toRack (b, session.document().paramValue (session.registry(), node, b.param));
    if (auto* p = state->getParameter (id); p != nullptr && std::abs (taken - static_cast<double> (value)) > 1.0e-4 * (1.0 + std::abs (taken)))
    {
        const juce::ScopedValueSetter<bool> guard (pulling, true);
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (taken)));
    }
}

double RackHost::hostSampleRate() const { return session.audio().sampleRate(); }
float RackHost::outputPeak() const noexcept { return outputPeakNow ? outputPeakNow() : -1.0f; }
float RackHost::hostTempo() const noexcept { return std::max (20.0f, session.audio().hostBpm()); }

const osp::plugin::rack::Faceplate* RackHost::rackFaceplate (int kind) const noexcept
{
    const auto it = plates.find (kind);
    return it != plates.end() ? &it->second : nullptr;
}

juce::String RackHost::rackPresetMemory (const juce::String& key) const
{
    const auto it = presetMemory.find (key);
    return it != presetMemory.end() ? it->second : juce::String();
}

void RackHost::setRackPresetMemory (const juce::String& key, const juce::String& code) { presetMemory[key] = code; }

} // namespace andor::modular::ui
