#pragma once

#include "graph/Events.h"
#include "graph/Types.h"

#include "engine/HostTiming.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace osp
{
struct InstrumentModel;
}

namespace andor::modular
{

//==============================================================================
// Descriptors: what a node type is (static, shared by every instance).

struct PortDesc
{
    std::string id;
    std::string name;
    PortKind kind = PortKind::audioMono;
    Polarity polarity = Polarity::bipolar;
    /** Several cables may arrive here and are summed (VOICE MIX, event inputs). Otherwise one
        cable: a second is an "ambiguous fan-in" and is refused with a Mixer suggestion. */
    bool multi = false;
    /** An audio-rate input that also accepts control signals (FM, ring, sync, pan CV). */
    bool acceptsControl = false;
    /** BOUNDARY nodes: this input is in the voice domain although the node is global. */
    bool voiceSide = false;
    /** A mono audio port is mono in the VOICE domain and stereo in the GLOBAL domain (a
        filter, drive or noise instance placed after VOICE MIX processes both channels). */
    bool stereoWhenGlobal = true;
    std::string unit;
};

/** A port's kind as one instance sees it (see PortDesc::stereoWhenGlobal). */
inline PortKind effectiveKind (const PortDesc& port, Domain nodeDomain) noexcept
{
    if (port.kind == PortKind::audioMono && port.stereoWhenGlobal && nodeDomain == Domain::global && ! port.voiceSide)
        return PortKind::audioStereo;
    return port.kind;
}

struct ParamDesc
{
    std::string id;
    std::string name;
    double min = 0.0, max = 1.0, def = 0.0;
    ParamCurve curve = ParamCurve::linear;
    std::vector<std::string> choices;   ///< ParamCurve::choice
    std::string unit;                   ///< "Hz", "s", "dB", "st", "%", "ct"
    bool modulatable = true;
    bool quick = false;                 ///< on the node's Quick face (2..4 per node)
    bool bipolarModulation = true;      ///< modulation adds +-depth (else 0..depth)
    /** Changing it rebuilds the node (an impulse, a network size): the editor recompiles the
        patch instead of sending the value live. Never modulatable. */
    bool structural = false;
    /** A detail of the module: edited in its rack, never on its tile, and left alone by EVOLVE
        (an effect's deeper settings: a compressor's knee, a tape's dropouts). */
    bool detail = false;
    /** Progressive disclosure: shown only while choice/toggle parameter `showIf` has one of
        the values in `showIfMask` (bit n = value n). Empty: always shown. */
    std::string showIf;
    std::uint32_t showIfMask = 0;

    double normalise (double v) const noexcept;
    double denormalise (double n) const noexcept;
    double clampNative (double v) const noexcept;
    std::string format (double v) const;
};

/** Relative cost units (1 = one plain CLASSIC oscillator voice at 48 kHz). Measured by
    tools/modular-bench and kept in docs/PERFORMANCE_BUDGET.md. */
struct CostProfile
{
    float units = 1.0f;
    bool heavy = false;     ///< shown as a CPU tag in Quick Add
};

enum class NodeRole : std::uint8_t
{
    plain,
    source,          ///< a sound generator (counted against the 6-source budget)
    output,          ///< the one true output
    boundary,        ///< VOICE MIX / POLY->GLOBAL: voice inputs, global outputs
    feedback,        ///< FEEDBACK DELAY: the only cycle-breaker
    event            ///< note-stream node
};

class Processor;
class ExternalInstrument;
class EventProcessor;
struct NodeShared;
struct PrepareContext;

struct NodeDesc
{
    std::string type;          ///< stable type id, e.g. "osc.analog" (saved: never rename)
    int version = 1;           ///< node state schema version
    std::string name;          ///< "ANALOG OSC"
    std::string category;      ///< Sources / Processors / Controllers / Spatial / Utilities
    std::string description;   ///< one line for Quick Add
    std::string help;          ///< a few lines for the detail panel
    std::vector<std::string> keywords;
    std::uint8_t domains = globalOnly;
    Domain defaultDomain = Domain::global;
    NodeRole role = NodeRole::plain;
    std::vector<PortDesc> inputs, outputs;
    std::vector<ParamDesc> params;
    std::vector<std::string> strings;   ///< string state keys (e.g. "sample", "pattern")
    CostProfile cost;
    bool endsVoice = false;    ///< an envelope/VCA: the voice ends when every such node is idle
    bool isArp = false, isMorph = false, isExternal = false;
    int latencySamples = 0;
    bool available = true;
    std::string unavailableReason;
    /** Voice/global processors (one per voice for VOICE nodes). */
    std::function<std::unique_ptr<Processor>()> create;
    /** Event-domain processors. */
    std::function<std::unique_ptr<EventProcessor>()> createEvent;
    /** Optional state shared by one node's voices (FFT tables, analysis), made at compile. */
    std::function<std::unique_ptr<NodeShared> (const PrepareContext&)> createShared;

    /** Whether a parameter applies with the node's current settings (see ParamDesc::showIf). */
    bool paramShown (const ParamDesc& p, const std::map<std::string, double>& values) const;
    int inputIndex (const std::string& id) const noexcept;
    int outputIndex (const std::string& id) const noexcept;
    int paramIndex (const std::string& id) const noexcept;
    bool supports (Domain d) const noexcept { return (domains & domainBit (d)) != 0; }
};

//==============================================================================
// Runtime contracts.

/** A loaded recording, as every sample-reading node sees it: immutable once published. */
struct SampleAsset
{
    enum class Status : std::uint8_t { none, missing, loading, provisional, ready, failed };
    Status status = Status::none;
    std::string name, path, hash, message;
    std::shared_ptr<const osp::InstrumentModel> model;   ///< OSP's prepared playable model
    double rootMidi = 60.0;
    /** How sure OSP's analysis is of that root, 0..1 ("moderate" from 0.4). Below it the
        recording has no pitch to speak of (speech, noise, a drum, a chord, a texture): a
        guessed root can lie octaves from the keys and play it too low to hear, so ORGANIC
        SAMPLE and GRANULAR then take C4 as the root (middle C plays it as recorded) until the
        musician sets ROOT. */
    double pitchConfidence = 0.0;
    bool pitched() const noexcept { return pitchConfidence >= 0.4; }
    double durationSeconds = 0.0;
    /** Rhythmic analysis (onset grid): tempo estimate and onset times (seconds). */
    double tempoBpm = 0.0;
    double beats = 0.0;
    std::vector<double> onsetsSeconds;
    /** A coarse mono overview for drawing (128 peaks 0..1). */
    std::vector<float> overview;
};

using AssetResolver = std::function<std::shared_ptr<const SampleAsset> (const std::string& key)>;

struct NodeShared
{
    virtual ~NodeShared() = default;
};

struct PrepareContext
{
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::uint64_t seed = 1;            ///< the node's own seed (patch seed + node id)
    const NodeShared* shared = nullptr;
    std::shared_ptr<const SampleAsset> asset;   ///< the node's "sample" string, resolved
    const std::vector<std::string>* strings = nullptr;   ///< the node's string state, in desc.strings order
    int voiceIndex = -1;               ///< -1 global
    const float* params = nullptr;     ///< the node's native parameter values at compile (structural ones)
    std::string nodeId;                ///< the node's stable id
    /** EXTERNAL INSTRUMENT: the owner's host (key -> instance); null: the global fallback. */
    const std::function<std::shared_ptr<ExternalInstrument> (const std::string& nodeId, const std::vector<std::string>& strings)>* externals = nullptr;
};

/** What a voice knows about its note. */
struct VoiceInfo
{
    int note = 60;
    double pitch = 60.0;         ///< MIDI pitch now (glide and bend included)
    float velocity = 0.8f;
    bool gate = false;           ///< key (or pedal) holding
    bool legato = false;         ///< this start glides from a held note (MONO LEGATO)
    std::uint32_t noteId = 0;
    std::uint64_t seed = 1;      ///< per note: patch seed, note counter, pitch
    int voiceIndex = 0;
    int age = 0;                 ///< samples since the start
    float fade = 1.0f;           ///< steal / end fade at the chunk start (applied at the boundary)
    float fadeEnd = 1.0f;        ///< ...and at its end (a linear ramp between)
};

struct ChunkContext
{
    double sampleRate = 48000.0;
    int numSamples = 0;
    int offset = 0;                     ///< chunk start inside the host block
    std::int64_t samplePosition = 0;    ///< engine sample clock at the chunk start
    double bpm = 120.0;
    double ppq = 0.0;                   ///< quarter notes at the chunk start (free-running when stopped)
    bool playing = false;
    bool transportJumped = false;       ///< this block's transport moved discontinuously
    const VoiceInfo* voice = nullptr;   ///< VOICE nodes
    const NoteEvent* events = nullptr;  ///< GLOBAL nodes with a notes input: this chunk's events
    int numEvents = 0;
    float pitchBendSemitones = 0.0f;
    std::array<const float*, 2> liveInput { nullptr, nullptr };   ///< host audio input at the chunk (LIVE INPUT)
    std::uint64_t generation = 0;       ///< the compiled graph's generation
};

struct ProcessIO
{
    std::array<std::array<const float*, 2>, maxPorts> in {};    ///< never null (zeros when unconnected)
    std::array<bool, maxPorts> inConnected {};
    std::array<std::array<float*, 2>, maxPorts> out {};
    const float* params = nullptr;      ///< resolved native values (base + macros + scenes + modulation)
    const std::vector<std::string>* strings = nullptr;
};

/** Live values a node shows (written by the audio thread, read by the UI at 20-30 Hz). */
struct Telemetry
{
    std::array<float, 16> values {};
    int count = 0;
};

class Processor
{
public:
    virtual ~Processor() = default;
    /** Allocates (message/compile thread). */
    virtual void prepare (const PrepareContext&) {}
    virtual void reset() noexcept {}
    /** VOICE: a note starts on this voice (retrigger false: a legato glide keeps state). */
    virtual void start (const VoiceInfo&, bool /*retrigger*/) noexcept {}
    /** VOICE: the key is released. */
    virtual void release() noexcept {}
    virtual void process (const ChunkContext& ctx, ProcessIO& io) noexcept = 0;
    /** endsVoice nodes: false once silent after release. */
    virtual bool active() const noexcept { return true; }
    /** BOUNDARY nodes: one voice's inputs added in (before process() runs globally). */
    virtual void accumulate (const ChunkContext&, const ProcessIO&, const VoiceInfo&) noexcept {}
    /** BOUNDARY nodes: called once per chunk before any voice accumulates. */
    virtual void beginChunk (int /*numSamples*/) noexcept {}
    /** FEEDBACK nodes: the chunk's input, written after everything else ran. */
    virtual void writeFeedback (const ChunkContext&, const ProcessIO&) noexcept {}
    /** UI telemetry (audio thread; fixed size). */
    virtual void telemetry (Telemetry&) const noexcept {}
    /** Samples of latency this node adds (planning). */
    virtual int latency() const noexcept { return 0; }
};

/** Event-domain processing, once per host block. */
struct EventBlock
{
    int numSamples = 0;
    double sampleRate = 48000.0;
    osp::HostTiming timing;
    bool transportStarted = false;
    std::int64_t samplePosition = 0;
};

class EventProcessor
{
public:
    virtual ~EventProcessor() = default;
    virtual void prepare (const PrepareContext&) {}
    virtual void reset() noexcept {}
    /** `inputs`: the merged events of each notes input; `params`: native values;
        `out`: this node's notes output. `controlOut` (optional): per-sample values of the
        node's control outputs for the block (pre-sized to the block). */
    virtual void process (const EventBlock& block, const EventList* const* inputs, int numInputs, const float* params, EventList& out,
                          float* const* controlOut, int numControlOuts) noexcept = 0;
    virtual void telemetry (Telemetry&) const noexcept {}
};

} // namespace andor::modular
