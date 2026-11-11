#pragma once

#include "graph/CompiledGraph.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace andor::modular
{

/**
    The real-time engine. One per plugin instance (or offline render).

    Message thread: prepare(), publish(), collectGarbage(), setMacro(), setScenePosition(),
    latest(). Audio thread: process() only. The audio thread never allocates, locks, frees or
    blocks: graphs arrive through an atomic pointer, leave through a fixed-size lock-free queue,
    and are destroyed by collectGarbage() on the message thread.

    Swaps: a new graph fades in over `swapFadeSamples` while the old one fades out (both run for
    that long); notes held on the keyboard are played again in the new graph at its first
    sample, so a patch edit never leaves a held chord silent or stuck.
*/
class Engine
{
public:
    static constexpr int swapFadeSamples = 384;
    static constexpr int retireCapacity = 64;

    Engine();
    ~Engine();

    /** Message thread, audio stopped. Graphs compiled for another rate must be republished. */
    void prepare (double sampleRate, int maxBlock);
    double sampleRate() const noexcept { return rate; }
    int maxBlock() const noexcept { return blockCapacity; }

    /** Message thread: hands a compiled graph to the audio thread. */
    void publish (std::unique_ptr<CompiledGraph> graph);
    /** Message thread: frees graphs the audio thread has finished with. */
    void collectGarbage();
    /** Message thread: the most recently published graph (parameters, telemetry). */
    CompiledGraph* latest() noexcept { return latestGraph; }
    const CompiledGraph* latest() const noexcept { return latestGraph; }

    void setMacro (int index, float value) noexcept;      ///< 0..1
    float macro (int index) const noexcept;
    void setScenePosition (float position) noexcept;      ///< 0..3
    float scenePosition() const noexcept { return scenePos.load (std::memory_order_relaxed); }
    void allNotesOff() noexcept { panic.store (true, std::memory_order_relaxed); }
    /** Runtime voice cap (CPU admission): voices beyond it are not started (0: no cap). */
    void setVoiceCap (int cap) noexcept { voiceCap.store (cap, std::memory_order_relaxed); }
    void resetTransportMemory() noexcept { restart.store (true, std::memory_order_relaxed); }
    /** Audio thread, before process(): the host's input for LIVE INPUT (null: none). The
        pointers must stay valid through the next process() call. */
    void setLiveInput (const float* left, const float* right) noexcept
    {
        liveLeft = left;
        liveRight = right;
    }

    /** Audio thread. `hostValues` (optional, hostBankSize entries): the bound host parameters
        0..1, or negative for "unchanged". Any block size (0 .. maxBlock, longer ones are split);
        the output is delayed by maxChunk samples (included in latencySamples()). */
    void process (float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing,
                  const float* hostValues = nullptr) noexcept;

    // Diagnostics (any thread).
    int activeVoices() const noexcept { return voicesNow.load (std::memory_order_relaxed); }
    std::uint64_t currentGeneration() const noexcept { return generationNow.load (std::memory_order_relaxed); }
    /** The graph's latency plus the engine's fixed grid delay (maxChunk). */
    int latencySamples() const noexcept { return latencyNow.load (std::memory_order_relaxed); }
    int swapsCompleted() const noexcept { return swaps.load (std::memory_order_relaxed); }
    float outputPeak() const noexcept { return peakNow.load (std::memory_order_relaxed); }

    /** The output's last samples (mono sum) for the PLAY visualisation; message thread copies. */
    static constexpr int scopeSize = 1024;
    void copyScope (float* dest, int count) const noexcept;

private:
    void renderEngineBlock (float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing,
                            const float* hostValues) noexcept;
    void renderGraph (CompiledGraph& g, float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi,
                      const osp::HostTiming& timing, bool firstBlock, const float* hostValues) noexcept;
    void runEvents (CompiledGraph& g, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing, bool firstBlock) noexcept;
    void applyControls (CompiledGraph& g, const float* hostValues) noexcept;
    void laneNoteEvent (CompiledGraph& g, Lane& lane, const NoteEvent& e) noexcept;
    void startVoice (CompiledGraph& g, Lane& lane, LaneVoice& voice, const NoteEvent& e, bool retrigger, bool resetState) noexcept;
    void renderLane (CompiledGraph& g, Lane& lane, int laneIndex, const ChunkContext& base) noexcept;
    void runInstance (CompiledNode& n, Instance& inst, const ChunkContext& ctx) noexcept;
    void retire (CompiledGraph* g) noexcept;

    double rate = 48000.0;
    int blockCapacity = 512;

    std::atomic<CompiledGraph*> pending { nullptr };
    CompiledGraph* current = nullptr;       ///< audio thread
    CompiledGraph* fading = nullptr;        ///< audio thread: the previous graph fading out
    int fadePosition = 0;
    bool currentFresh = false;
    CompiledGraph* latestGraph = nullptr;   ///< message thread
    std::vector<std::unique_ptr<CompiledGraph>> owned;   ///< message thread: every live graph

    std::array<std::atomic<CompiledGraph*>, retireCapacity> retired {};
    std::atomic<int> retireWrite { 0 };

    std::array<std::atomic<float>, maxMacros> macros {};
    std::atomic<float> scenePos { 0.0f };
    float lastScenePos = -1.0f;
    std::atomic<bool> panic { false }, restart { false };
    std::atomic<int> voiceCap { 0 };

    // Keyboard memory (to replay held notes into a new graph).
    std::array<std::array<float, 128>, 16> held {};
    bool sustainDown = false;
    float bendSemitones = 0.0f;

    std::array<std::array<bool, 128>, 16> sustained {};
    EventList hostEvents;
    std::vector<std::uint8_t> marks;
    std::vector<float> scratchL, scratchR;
    std::array<MidiMessage, 2048 + 128> replay {};

    std::int64_t sampleClock = 0;
    double freePpq = 0.0;
    double lastPpqEnd = -1.0;

    std::array<std::atomic<float>, scopeSize> scope {};
    int scopeWrite = 0;

    std::atomic<int> voicesNow { 0 }, latencyNow { 0 }, swaps { 0 };
    std::atomic<std::uint64_t> generationNow { 0 };
    std::atomic<float> peakNow { 0.0f };
    float limiterGain = 1.0f;
    const float* liveLeft = nullptr;
    const float* liveRight = nullptr;

    // The fixed grid (see process()).
    struct QueuedMidi
    {
        std::int64_t time = 0;
        MidiMessage message;
    };
    std::array<QueuedMidi, 4096> queue {};
    int queueHead = 0, queueCount = 0;
    std::array<MidiMessage, 512> blockMidi {};
    std::vector<float> outLeft, outRight, inLeft, inRight;
    std::array<float, maxChunk> liveChunkL {}, liveChunkR {};
    const float* liveBlockLeft = nullptr;
    std::int64_t hostClock = 0, engineClock = 0, blockStart = 0;
    osp::HostTiming blockTiming;
    const float* hostValuesNow = nullptr;
    float blockPeak = 0.0f;
};

} // namespace andor::modular
