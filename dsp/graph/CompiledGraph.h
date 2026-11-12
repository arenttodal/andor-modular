#pragma once

#include "graph/Document.h"
#include "graph/Node.h"
#include "graph/Validator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace andor::modular
{

/**
    The executable form of a Document: immutable topology, prepared processors, wired buffers.
    Built off the audio thread (GraphCompiler), handed over lock-free (Engine::publish), owned by
    the audio thread while it plays, destroyed on the message thread (Engine::collectGarbage).

    Nothing in here grows after compile: every vector is sized by the compiler; the audio thread
    only writes sample buffers, voice state, smoothed values and telemetry atomics.
    docs/architecture.md ("Compiled graph") explains the layout.
*/

struct PortBuffer
{
    alignas (16) std::array<std::array<float, maxChunk>, 2> ch {};
};

/** A parameter modulation input of one instance (resolved per chunk). */
struct ModTerm
{
    int param = 0;
    const float* source = nullptr;   ///< the source port's buffer (chunk-relative)
    float depth = 1.0f;
};

/** An input fed by several cables (multi): summed into `sum` before the node runs. */
struct SumInput
{
    int port = 0;
    int channels = 1;
    std::vector<std::array<const float*, 2>> sources;
    PortBuffer* sum = nullptr;
};

/** Latency alignment of one input (a shorter parallel path delayed to meet a longer one). */
struct AlignInput
{
    int port = 0;
    int delay = 0;
    int channels = 1;
    std::vector<float> line;   ///< 2 * (delay + maxChunk) samples, interleaved by channel halves
    int write = 0;
    PortBuffer* out = nullptr;
    std::array<const float*, 2> source {};
};

struct Instance
{
    std::unique_ptr<Processor> processor;
    std::vector<PortBuffer> outputs;
    std::vector<PortBuffer> scratch;        ///< sums and alignment outputs
    std::vector<SumInput> sums;
    std::vector<AlignInput> aligns;
    std::vector<ModTerm> mods;
    std::vector<float> resolved;            ///< native parameter values this chunk
    ProcessIO io;
    /** Where this instance is between bypassed (0) and running (1), and its audible gain: both
        ramped by the engine over a few milliseconds (negative: not yet set). */
    float mixNow = -1.0f, gainNow = -1.0f;
};

/** A boundary node's view of one lane: per voice, the voice-side inputs. */
struct BoundaryFeed
{
    int lane = -1;
    std::vector<ProcessIO> voiceIO;                     ///< [voice]
    std::vector<std::vector<SumInput>> voiceSums;       ///< [voice] (several sources in one lane)
    std::vector<std::vector<PortBuffer>> voiceScratch;  ///< [voice]
};

struct MacroTarget
{
    int node = -1, param = -1;   ///< a parameter...
    int channel = -1;            ///< ...or a group control channel
    float min = 0.0f, max = 1.0f;
    MacroCurve curve = MacroCurve::linear;
};

struct ControlChannel
{
    std::string name;
    std::atomic<float> value { 0.0f };   ///< 0..1 (macro or group control base)
    float smoothed = 0.0f, coef = 1.0f;
    float effective = 0.0f;              ///< after upstream channels (a macro driving a group control)
    std::vector<MacroTarget> targets;
    bool hasDriver = false;
};

struct SceneTarget
{
    int node = -1, param = -1;
    std::array<float, sceneCount> norm {};
    std::uint8_t present = 0;            ///< bit per scene that captured it
    bool stepped = false;
};

struct CompiledNode
{
    std::string id;
    const NodeDesc* desc = nullptr;      ///< null: a missing module (inert)
    Domain domain = Domain::global;
    bool enabled = true;                 ///< at compile (event nodes: structural)
    /** Audio and control nodes, now: on, or bypassed. Set without a recompile (setEnabled):
        the engine crossfades between the processed signal and the bypass over ~12 ms, so a
        bypass never restarts a note or an effect's tail. */
    std::atomic<bool> live { true };
    /** A source's audible contribution, 0..1 (mute and solo: setGain), ramped by the engine at
        the source's own output, before anything downstream. */
    std::atomic<float> gain { 1.0f };
    int lane = -1;                       ///< VOICE nodes
    std::vector<std::string> strings;
    std::unique_ptr<NodeShared> shared;
    std::unique_ptr<std::atomic<float>[]> base;   ///< native values the message thread / host set
    std::vector<float> baseNative, baseNorm;      ///< this block (after macros)
    std::unique_ptr<std::atomic<float>[]> shown;  ///< baseNative published for the UI (knobs follow macros and scenes)
    std::vector<std::uint8_t> macroMapped;
    std::vector<Instance> instances;              ///< 1 (global) or polyphony (voice)
    // global nodes with a notes input
    bool takesNotes = false;
    EventList notes;
    int noteCursor = 0;
    // event nodes
    std::unique_ptr<EventProcessor> eventProcessor;
    EventList eventOut;
    std::vector<EventList> eventInputs;           ///< merged per notes input
    std::vector<std::vector<float>> eventControl; ///< block-length control outputs
    std::vector<std::vector<std::array<int, 3>>> eventSources;   ///< per input: {node, port(unused), transpose}
    // boundary
    std::vector<BoundaryFeed> feeds;
    // telemetry (audio writes, UI reads)
    std::array<std::atomic<float>, 16> telemetry {};
    std::atomic<int> telemetryCount { 0 };
    std::atomic<float> peak { 0.0f };
    int latency = 0;                              ///< accumulated path latency at the output
};

struct NoteSource
{
    int node = -1;        ///< event node index
    int transpose = 0;
    std::uint32_t salt = 0;
};

struct LaneVoice
{
    enum class State : std::uint8_t { free, active, released, stealing };
    State state = State::free;
    VoiceInfo info;
    std::uint64_t order = 0;     ///< start order (oldest steals first)
    double targetPitch = 60.0;
    int fadeRemaining = 0;       ///< samples left in a steal / end fade
    float fadeFrom = 1.0f;
    bool hasPending = false, pendingReleased = false;
    NoteEvent pending {};
    std::uint32_t laneNoteId = 0;
};

struct Lane
{
    std::vector<int> nodes;               ///< compiled node indices, topological
    std::vector<int> endsVoice;           ///< nodes whose activity keeps a voice alive
    std::vector<NoteSource> noteSources;
    std::vector<std::pair<int, int>> boundaries;   ///< {boundary node, feed index}
    std::vector<LaneVoice> voices;
    EventList events;
    int cursor = 0;
    std::uint64_t counter = 0;
    std::array<std::uint32_t, 32> heldIds {};      ///< MONO: the held stack (laneNoteIds)
    std::array<NoteEvent, 32> held {};
    int heldCount = 0;
    std::atomic<int> activeVoices { 0 };
    std::string key;                      ///< stable identity (sorted node ids) for swaps
};

/** One step of the chunk schedule: a global node, or a whole lane. */
struct Unit
{
    bool lane = false;
    int index = 0;
};

struct HostBindingTarget
{
    int node = -1, param = -1;
    float last = -1.0f;
};

struct CompiledGraph
{
    std::uint64_t generation = 0;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    PatchSettings settings;
    std::vector<CompiledNode> nodes;
    std::vector<int> eventOrder;          ///< event node indices, topological
    std::vector<Lane> lanes;
    std::vector<Unit> schedule;           ///< globals and lanes, topological
    std::vector<int> boundaryNodes;
    std::vector<int> feedbackNodes;
    int outputNode = -1;
    int keyboardNodes = 0;
    int latencySamples = 0;               ///< reported to the host
    std::vector<std::unique_ptr<ControlChannel>> channels;   ///< macros [0..7], then group controls
    std::vector<int> channelOrder;
    std::vector<SceneTarget> sceneTargets;
    std::array<std::array<float, maxMacros>, sceneCount> sceneMacros {};
    std::uint8_t scenesCaptured = 0;
    std::array<HostBindingTarget, hostBankSize> bindings {};
    ValidationReport report;
    double compileMilliseconds = 0.0;
    std::map<std::string, int> nodeIndexById;   ///< message thread lookups
    PortBuffer zeros {};
    std::vector<std::vector<float>> eventScratch;

    // Message thread: a parameter's new base value (native). Returns false if unknown.
    bool setParam (const std::string& node, const std::string& param, double native) noexcept;
    double getParam (const std::string& node, const std::string& param) const noexcept;
    /** The value in effect (base, then macros, scenes and automation), as of the last block. */
    double effectiveParam (const std::string& node, const std::string& param) const noexcept;
    /** Message thread: bypass / un-bypass an audio or control node, click-free (see CompiledNode::live). */
    bool setEnabled (const std::string& node, bool on) noexcept;
    /** Message thread: a source's audible gain 0..1 (mute, solo), ramped. */
    bool setGain (const std::string& node, float gain) noexcept;
    /** Message thread: a group control's value (0..1). */
    bool setChannel (int channel, float value) noexcept;
    int channelIndex (const std::string& groupId, int control) const noexcept;
    std::map<std::string, int> groupChannelBase;
};

} // namespace andor::modular
