#pragma once

#include "Session.h"

#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace andor::modular::ui
{

/** One control of a family rack, bound to one parameter of one module of the patch: the rack
    knows it by its ID in the ANDOR parameter schema ("echo.feedback"); the module by its own
    ("feedback"). `toRack` / `toNode` convert the value between the two (units: percent and
    0..1, choice orders); unset: `rack = node * scale + offset`. */
struct RackParam
{
    juce::String rackId;
    std::string param;
    double scale = 1.0, offset = 0.0;
    std::function<double (double node)> toRack;
    std::function<double (double rack)> toNode;
};

/**
    The family's racks (shared/andor-ui: ECHO, SPACE, MOVEMENT; ANDOR Analog's FILTER, MORPH and
    effects) are components that read and write an osp::plugin::InstrumentHost: a product's
    parameter state under the ANDOR IDs, and a few live views. In OSP and Analog the host is the
    instrument itself, so a rack edits *the* echo. In Modular a patch has any number of echoes:
    a rack is given a RackHost of its own, which is that interface for exactly ONE module.

      - It owns a private parameter state with the IDs the rack binds to (never the plugin's
        host parameters: nothing here is automated or saved).
      - Every bound ID is one parameter of this module (RackParam). A control moved in the rack
        becomes Session::setParam on this module (a gesture: one undo step); a change of the
        module (its tile's knob, a macro, undo, a scene) is pulled back into the rack's state.
      - What the rack asks about the instrument is answered for this module from the running
        graph (tempo, sample rate, telemetry), or with "nothing" where Modular has no such thing
        (layers, presets of the instrument, the modulation matrix).

    Nothing of ANDOR Analog's engine or the shell is constructed or cast to. Message thread.
*/
class RackHost final : public osp::plugin::InstrumentHost, private juce::AudioProcessorValueTreeState::Listener, private juce::AudioProcessorParameter::Listener
{
public:
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    /** `layout`: the rack's schema (the ANDOR layout, a product's additions, or this module's
        own); `binding`: which of its IDs are this module's parameters. */
    RackHost (Session& session, std::string nodeId, Layout layout, std::vector<RackParam> binding);
    ~RackHost() override;

    const std::string& nodeId() const noexcept { return node; }
    /** The module's values into the rack's state (after any change of the patch). */
    void pull();
    /** A rack ID's module parameter ("" if it is not bound). */
    std::string paramFor (const juce::String& rackId) const;
    /** Rack IDs that are held at a value and never reach the module (a macro's amount at 100 %
        where the module has its own MIX, a switch the module does not have). */
    void hold (const juce::String& rackId, float value);
    /** The faceplate racks of `kind` wear (rack::Kind as int). */
    void setFaceplate (int kind, osp::plugin::rack::Faceplate plate) { plates[kind] = std::move (plate); }
    /** Live answers (set by the rack's factory for the module it shows). */
    std::function<float()> shaperPhaseNow, outputPeakNow;
    std::function<juce::String (const juce::String& topic)> statusNow;

    // -- InstrumentHost ----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState& apvts() override { return *state; }
    const juce::AudioProcessorValueTreeState& apvts() const override { return *state; }
    juce::UndoManager& undo() override { return unused; }
    double hostSampleRate() const override;
    float hostTempo() const noexcept override;
    float outputPeak() const noexcept override;
    const osp::plugin::rack::Faceplate* rackFaceplate (int kind) const noexcept override;
    float shaperPhase() const noexcept override { return shaperPhaseNow ? shaperPhaseNow() : -1.0f; }
    juce::String engineStatus (const juce::String& topic) const override { return statusNow ? statusNow (topic) : juce::String(); }
    juce::String rackPresetMemory (const juce::String& key) const override;
    void setRackPresetMemory (const juce::String& key, const juce::String& code) override;
    juce::String productWordmark() const override { return "ANDOR/MODULAR"; }

    // (Modular has no A / B / C layers, instrument presets or modulation matrix: the racks
    // never ask for them; these answer "none".)
    LoadState loadState (int) const noexcept override { return LoadState::empty; }
    std::shared_ptr<const osp::plugin::LoadedInstrument> currentInstrument (int) const override { return {}; }
    std::optional<double> rootOverride (int) const override { return {}; }
    const osp::InstrumentEngine::GrainSnapshot& grainSnapshot (int) const noexcept override { return noGrains; }
    bool isLayerOccupied (int) const override { return false; }
    juce::String presetDisplayName() const override { return {}; }
    void stepPresetList (int) override {}
    std::vector<PresetEntry> presetList() const override { return {}; }
    void openPresetEntry (const PresetEntry&) override {}
    bool isFavourite() const override { return false; }
    void toggleFavourite() override {}
    void initPatch() override {}
    void resetSettings() override {}
    bool canRestorePreviousState() const noexcept override { return false; }
    juce::String previousStateDescription() const override { return {}; }
    bool restorePreviousState() override { return false; }
    osp::mod::Curve modulationCurve (int) const override { return {}; }
    void setModulationCurve (int, const osp::mod::Curve&, bool) override {}
    osp::InstrumentEngine::ModView modulationView() const noexcept override { return {}; }
    int modControllerNumber (int) const noexcept override { return -1; }
    void setModControllerNumber (int, int) noexcept override {}
    void learnModController (int) noexcept override {}
    int learningModController() const noexcept override { return -1; }
    bool velocityAppliesToAmp() const noexcept override { return true; }
    void setVelocityAppliesToAmp (bool) noexcept override {}
    bool arpRateIsStepped() const noexcept override { return true; }
    void setArpRateStepped (bool) noexcept override {}
    int recentVelocityAt (int) const noexcept override { return 0; }
    int playedVelocityCount() const noexcept override { return 0; }
    int lastPlayedNoteNumber() const noexcept override { return -1; }
    ArpView arpView() const noexcept override { return {}; }
    int editLayer() const noexcept override { return 0; }
    osp::ShaperPattern shaperCustomPattern() const override { return {}; }
    void setShaperCustomPattern (const osp::ShaperPattern&, bool) override {}
    bool shaperEditorLarge() const noexcept override { return false; }
    void setShaperEditorLarge (bool) noexcept override {}

private:
    struct Carrier;   ///< the parameter state's owner (an AudioProcessor that is never played)
    void parameterChanged (const juce::String& id, float value) override;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    void touch();
    double toRack (const RackParam&, double nodeValue) const;
    double toNode (const RackParam&, double rackValue) const;

    Session& session;
    std::string node;
    std::vector<RackParam> bound;
    std::map<juce::String, std::size_t> byRackId;
    std::map<juce::String, float> held;
    std::map<int, osp::plugin::rack::Faceplate> plates;
    std::map<juce::String, juce::String> presetMemory;
    std::unique_ptr<Carrier> carrier;
    std::unique_ptr<juce::AudioProcessorValueTreeState> state;
    juce::UndoManager unused;
    osp::InstrumentEngine::GrainSnapshot noGrains;
    bool pulling = false, gesture = false;
    double lastTouchMs = -1000.0;
};

} // namespace andor::modular::ui
