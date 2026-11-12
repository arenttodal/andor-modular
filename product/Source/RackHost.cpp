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
        // Compared in the control's own units, never normalised: a strongly skewed range (ANDOR
        // Analog's COMPRESSOR THRESHOLD) squeezes most of its values into a sliver near 0.
        const auto& range = p->getNormalisableRange();
        const float wanted = juce::jlimit (range.start, range.end, static_cast<float> (b.read ? b.read() : toRack (b, doc.paramValue (session.registry(), node, b.param))));
        const float shown = p->convertFrom0to1 (p->getValue());
        if (std::abs (shown - wanted) > 1.0e-6f * std::max (1.0f, range.end - range.start))
            p->setValueNotifyingHost (p->convertTo0to1 (wanted));
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
    if (b.write)
    {
        b.write (static_cast<double> (value));
        return;
    }
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

// What a module's rack remembers between openings (the preset it shows, its page) is kept with
// the session, by module: two echoes never share a preset name.
juce::String RackHost::rackPresetMemory (const juce::String& key) const
{
    const auto& memory = session.rackMemory (node).presets;
    const auto it = memory.find (key);
    return it != memory.end() ? it->second : juce::String();
}

void RackHost::setRackPresetMemory (const juce::String& key, const juce::String& code) { session.rackMemory (node).presets[key] = code; }

// (The module's own name: two compressors are never both "COMPRESSOR 1".)
juce::String RackHost::fxRackTitle (andor::analog::fx::InstanceId) const
{
    const auto* n = session.document().findNode (node);
    return n != nullptr ? juce::String (session.document().displayName (session.registry(), *n)).toUpperCase() : juce::String();
}

andor::analog::FxRackSource::Memory* RackHost::fxRackMemory (andor::analog::fx::InstanceId) { return &session.rackMemory (node).fx; }

} // namespace andor::modular::ui
